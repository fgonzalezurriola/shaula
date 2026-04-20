#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

CPU_MAX=0.5
RSS_MAX_MB=40
SAMPLES=30
WARMUP=4
INTERVAL_SEC=0.2
JSON_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cpu-max) CPU_MAX="$2"; shift 2 ;;
    --rss-max-mb) RSS_MAX_MB="$2"; shift 2 ;;
    --samples) SAMPLES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --interval-sec) INTERVAL_SEC="$2"; shift 2 ;;
    --json-only) JSON_ONLY=1; shift ;;
    *) echo "ERR_PERF_USAGE script=benchmark-daemon-idle unknown_flag=$1" >&2; exit 1 ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

if [[ ! "${SAMPLES}" =~ ^[0-9]+$ ]] || [[ ! "${WARMUP}" =~ ^[0-9]+$ ]]; then
  echo "ERR_PERF_USAGE script=benchmark-daemon-idle reason=non_numeric_samples_or_warmup" >&2
  exit 1
fi

if (( SAMPLES < 30 )); then
  echo "ERR_PERF_USAGE script=benchmark-daemon-idle reason=samples_below_minimum min=30 actual=${SAMPLES}" >&2
  exit 1
fi

if (( WARMUP < 0 )); then
  echo "ERR_PERF_USAGE script=benchmark-daemon-idle reason=negative_warmup actual=${WARMUP}" >&2
  exit 1
fi

zig build >/dev/null

python3 - "$CPU_MAX" "$RSS_MAX_MB" "$SAMPLES" "$WARMUP" "$INTERVAL_SEC" "$JSON_ONLY" <<'PY'
import json
import math
import subprocess
import sys
import time
from pathlib import Path

cpu_max = float(sys.argv[1])
rss_max_mb = float(sys.argv[2])
samples = int(sys.argv[3])
warmup = int(sys.argv[4])
interval = float(sys.argv[5])
json_only = int(sys.argv[6]) == 1

socket_path = f"/tmp/shaula-task11-daemon-{int(time.time() * 1000)}.sock"
start_cmd = ["./zig-out/bin/shaula", "daemon", "start", "--json", "--socket", socket_path]
stop_cmd = ["./zig-out/bin/shaula", "daemon", "stop", "--json", "--socket", socket_path]

start_proc = subprocess.run(start_cmd, capture_output=True, text=True)
out = (start_proc.stdout or "").strip()
err = (start_proc.stderr or "").strip()
start_output = out if out else err
if start_proc.returncode != 0:
    print(f"ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=start_failed output={start_output}", file=sys.stderr)
    sys.exit(1)

try:
    payload = json.loads(start_output)
except Exception:
    print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=start_invalid_json", file=sys.stderr)
    sys.exit(1)

if payload.get("ok") is not True:
    print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=start_non_ok_json", file=sys.stderr)
    sys.exit(1)

pid = None
for _ in range(80):
    probe = subprocess.run(["pgrep", "-f", f"shaula daemon _serve --json --socket {socket_path}"], capture_output=True, text=True)
    if probe.returncode == 0:
        lines = [line.strip() for line in probe.stdout.splitlines() if line.strip()]
        if lines:
            pid = int(lines[-1])
            break
    time.sleep(0.05)

if pid is None:
    subprocess.run(stop_cmd, capture_output=True, text=True)
    print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=pid_not_found", file=sys.stderr)
    sys.exit(1)

def read_proc(pid_value: int):
    stat_path = Path(f"/proc/{pid_value}/stat")
    status_path = Path(f"/proc/{pid_value}/status")
    if not stat_path.exists() or not status_path.exists():
        return None

    stat_raw = stat_path.read_text()
    tail = stat_raw.rsplit(")", 1)[-1].strip().split()
    utime_ticks = float(tail[11])
    stime_ticks = float(tail[12])

    rss_kb = None
    for line in status_path.read_text().splitlines():
        if line.startswith("VmRSS:"):
            parts = line.split()
            if len(parts) >= 2 and parts[1].isdigit():
                rss_kb = float(parts[1])
            break
    if rss_kb is None:
        rss_kb = 0.0

    return (utime_ticks + stime_ticks, rss_kb)

hz = float(subprocess.check_output(["getconf", "CLK_TCK"], text=True).strip())

for _ in range(warmup):
    if read_proc(pid) is None:
        subprocess.run(stop_cmd, capture_output=True, text=True)
        print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=daemon_exited_during_warmup", file=sys.stderr)
        sys.exit(1)
    time.sleep(interval)

cpu_samples = []
rss_samples_mb = []
prev = read_proc(pid)
if prev is None:
    subprocess.run(stop_cmd, capture_output=True, text=True)
    print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=daemon_unavailable_before_sampling", file=sys.stderr)
    sys.exit(1)
prev_ticks, _ = prev

for _ in range(samples):
    time.sleep(interval)
    current = read_proc(pid)
    if current is None:
        subprocess.run(stop_cmd, capture_output=True, text=True)
        print("ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=daemon_exited_during_sampling", file=sys.stderr)
        sys.exit(1)
    ticks, rss_kb = current
    delta_ticks = max(0.0, ticks - prev_ticks)
    prev_ticks = ticks
    cpu_percent = (delta_ticks / hz) / interval * 100.0
    cpu_samples.append(cpu_percent)
    rss_samples_mb.append(rss_kb / 1024.0)

stop_proc = subprocess.run(stop_cmd, capture_output=True, text=True)
if stop_proc.returncode != 0:
    stop_text = (stop_proc.stdout or "").strip() or (stop_proc.stderr or "").strip()
    print(f"ERR_PERF_MEASUREMENT_FAILED benchmark=daemon_idle reason=stop_failed output={stop_text}", file=sys.stderr)
    sys.exit(1)

def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    rank = math.ceil((p / 100.0) * len(sorted_values)) - 1
    idx = min(max(rank, 0), len(sorted_values) - 1)
    return sorted_values[idx]

cpu_sorted = sorted(cpu_samples)
rss_sorted = sorted(rss_samples_mb)

cpu_avg = sum(cpu_samples) / len(cpu_samples)
rss_p95 = percentile(rss_sorted, 95)
rss_peak = max(rss_samples_mb) if rss_samples_mb else 0.0

passed = cpu_avg <= cpu_max and rss_p95 <= rss_max_mb
result = {
    "benchmark": "daemon_idle_footprint",
    "samples": samples,
    "warmup": warmup,
    "interval_sec": interval,
    "metrics": {
        "cpu_avg_percent": round(cpu_avg, 3),
        "rss_p95_mb": round(rss_p95, 3),
        "rss_peak_mb": round(rss_peak, 3),
    },
    "thresholds": {
        "cpu_max_percent": cpu_max,
        "rss_p95_max_mb": rss_max_mb,
    },
    "pass": passed,
    "error_token": None if passed else "ERR_PERF_BUDGET_EXCEEDED",
}

if json_only:
    print(json.dumps(result, separators=(",", ":")))
else:
    print(json.dumps(result, indent=2))

if not passed:
    print(
        f"ERR_PERF_BUDGET_EXCEEDED benchmark=daemon_idle_footprint cpu_avg_percent={result['metrics']['cpu_avg_percent']} cpu_max_percent={cpu_max} rss_p95_mb={result['metrics']['rss_p95_mb']} rss_p95_max_mb={rss_max_mb}",
        file=sys.stderr,
    )
    sys.exit(1)
PY
