#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

SAMPLES=60
WARMUP=8
AREA_P95_MAX=150
WINDOW_P95_MAX=220
JSON_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --area-p95-max-ms) AREA_P95_MAX="$2"; shift 2 ;;
    --window-p95-max-ms) WINDOW_P95_MAX="$2"; shift 2 ;;
    --json-only) JSON_ONLY=1; shift ;;
    *) echo "ERR_PERF_USAGE script=benchmark-capture-completion unknown_flag=$1" >&2; exit 1 ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

if [[ ! "${SAMPLES}" =~ ^[0-9]+$ ]] || [[ ! "${WARMUP}" =~ ^[0-9]+$ ]]; then
  echo "ERR_PERF_USAGE script=benchmark-capture-completion reason=non_numeric_samples_or_warmup" >&2
  exit 1
fi

if (( SAMPLES < 30 )); then
  echo "ERR_PERF_USAGE script=benchmark-capture-completion reason=samples_below_minimum min=30 actual=${SAMPLES}" >&2
  exit 1
fi

if (( WARMUP < 0 )); then
  echo "ERR_PERF_USAGE script=benchmark-capture-completion reason=negative_warmup actual=${WARMUP}" >&2
  exit 1
fi

./dev build >/dev/null

python3 - "$SAMPLES" "$WARMUP" "$AREA_P95_MAX" "$WINDOW_P95_MAX" "$JSON_ONLY" <<'PY'
import json
import math
import subprocess
import sys

samples = int(sys.argv[1])
warmup = int(sys.argv[2])
area_target = float(sys.argv[3])
window_target = float(sys.argv[4])
json_only = int(sys.argv[5]) == 1

def run_and_extract(command: list[str], bench_name: str) -> float:
    proc = subprocess.run(command, capture_output=True, text=True)
    out = (proc.stdout or "").strip()
    err = (proc.stderr or "").strip()
    payload_text = out if out else err
    if proc.returncode != 0:
        print(f"ERR_PERF_MEASUREMENT_FAILED benchmark={bench_name} rc={proc.returncode} output={payload_text}", file=sys.stderr)
        sys.exit(1)
    try:
        payload = json.loads(payload_text)
    except Exception:
        print(f"ERR_PERF_MEASUREMENT_FAILED benchmark={bench_name} reason=invalid_json", file=sys.stderr)
        sys.exit(1)
    if payload.get("ok") is not True:
        print(f"ERR_PERF_MEASUREMENT_FAILED benchmark={bench_name} reason=non_ok_json", file=sys.stderr)
        sys.exit(1)
    latency = payload.get("latency_ms")
    if not isinstance(latency, (int, float)):
        print(f"ERR_PERF_MEASUREMENT_FAILED benchmark={bench_name} reason=latency_missing", file=sys.stderr)
        sys.exit(1)
    return float(latency)

area_cmd = ["./build/shaula", "capture", "area", "--json", "--no-preview"]
window_cmd = ["./build/shaula", "capture", "window", "--json", "--window-id", "bench-window"]

for _ in range(warmup):
    run_and_extract(area_cmd, "capture_completion_area")
    run_and_extract(window_cmd, "capture_completion_window")

area_values: list[float] = []
window_values: list[float] = []

for _ in range(samples):
    area_values.append(run_and_extract(area_cmd, "capture_completion_area"))
    window_values.append(run_and_extract(window_cmd, "capture_completion_window"))

area_values.sort()
window_values.sort()

def percentile(sorted_values: list[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    rank = math.ceil((p / 100.0) * len(sorted_values)) - 1
    idx = min(max(rank, 0), len(sorted_values) - 1)
    return sorted_values[idx]

area_p50 = percentile(area_values, 50)
area_p95 = percentile(area_values, 95)
window_p50 = percentile(window_values, 50)
window_p95 = percentile(window_values, 95)

passed = area_p95 <= area_target and window_p95 <= window_target

result = {
    "benchmark": "capture_completion",
    "samples": samples,
    "warmup": warmup,
    "metrics_ms": {
        "area": {
            "p50": round(area_p50, 2),
            "p95": round(area_p95, 2),
        },
        "window": {
            "p50": round(window_p50, 2),
            "p95": round(window_p95, 2),
        },
    },
    "thresholds_ms": {
        "area_p95_max": area_target,
        "window_p95_max": window_target,
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
        f"ERR_PERF_BUDGET_EXCEEDED benchmark=capture_completion area_p95_ms={result['metrics_ms']['area']['p95']} area_p95_max={area_target} window_p95_ms={result['metrics_ms']['window']['p95']} window_p95_max={window_target}",
        file=sys.stderr,
    )
    sys.exit(1)
PY
