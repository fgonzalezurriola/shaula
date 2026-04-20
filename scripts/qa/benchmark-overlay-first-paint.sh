#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

SAMPLES=100
WARMUP=10
P95_MAX=75
P99_MAX=110
JSON_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --p95-max-ms) P95_MAX="$2"; shift 2 ;;
    --p99-max-ms) P99_MAX="$2"; shift 2 ;;
    --json-only) JSON_ONLY=1; shift ;;
    *) echo "ERR_PERF_USAGE script=benchmark-overlay-first-paint unknown_flag=$1" >&2; exit 1 ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

if [[ ! "${SAMPLES}" =~ ^[0-9]+$ ]] || [[ ! "${WARMUP}" =~ ^[0-9]+$ ]]; then
  echo "ERR_PERF_USAGE script=benchmark-overlay-first-paint reason=non_numeric_samples_or_warmup" >&2
  exit 1
fi

if (( SAMPLES < 30 )); then
  echo "ERR_PERF_USAGE script=benchmark-overlay-first-paint reason=samples_below_minimum min=30 actual=${SAMPLES}" >&2
  exit 1
fi

if (( WARMUP < 0 )); then
  echo "ERR_PERF_USAGE script=benchmark-overlay-first-paint reason=negative_warmup actual=${WARMUP}" >&2
  exit 1
fi

zig build >/dev/null

python3 - "$SAMPLES" "$WARMUP" "$P95_MAX" "$P99_MAX" "$JSON_ONLY" <<'PY'
import json
import math
import subprocess
import sys
import time

samples = int(sys.argv[1])
warmup = int(sys.argv[2])
p95_target = float(sys.argv[3])
p99_target = float(sys.argv[4])
json_only = int(sys.argv[5]) == 1

base_cmd = ["./zig-out/bin/shaula", "capture", "area", "--dry-run", "--json"]

def run_once() -> tuple[float, int, str]:
    t0 = time.perf_counter_ns()
    proc = subprocess.run(base_cmd, capture_output=True, text=True)
    t1 = time.perf_counter_ns()
    out = (proc.stdout or "").strip()
    err = (proc.stderr or "").strip()
    output = out if out else err
    return (t1 - t0) / 1_000_000.0, proc.returncode, output

for _ in range(warmup):
    run_once()

measurements: list[float] = []
for _ in range(samples):
    ms, rc, output = run_once()
    if rc != 0:
      print(f"ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint rc={rc} output={output}", file=sys.stderr)
      sys.exit(1)
    try:
      payload = json.loads(output)
      if payload.get("ok") is not True:
        print("ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint reason=non_ok_json", file=sys.stderr)
        sys.exit(1)
    except Exception:
      print("ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint reason=invalid_json", file=sys.stderr)
      sys.exit(1)
    measurements.append(ms)

measurements.sort()

def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    rank = math.ceil((p / 100.0) * len(sorted_values)) - 1
    idx = min(max(rank, 0), len(sorted_values) - 1)
    return sorted_values[idx]

p50 = percentile(measurements, 50)
p95 = percentile(measurements, 95)
p99 = percentile(measurements, 99)
mean = sum(measurements) / len(measurements)

passed = p95 <= p95_target and p99 <= p99_target
result = {
    "benchmark": "overlay_first_paint",
    "samples": samples,
    "warmup": warmup,
    "metrics_ms": {
        "p50": round(p50, 2),
        "p95": round(p95, 2),
        "p99": round(p99, 2),
        "mean": round(mean, 2),
    },
    "thresholds_ms": {
        "p95_max": p95_target,
        "p99_max": p99_target,
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
        f"ERR_PERF_BUDGET_EXCEEDED benchmark=overlay_first_paint p95_ms={result['metrics_ms']['p95']} p95_max={p95_target} p99_ms={result['metrics_ms']['p99']} p99_max={p99_target}",
        file=sys.stderr,
    )
    sys.exit(1)
PY
