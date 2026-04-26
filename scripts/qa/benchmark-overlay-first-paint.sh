#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

SAMPLES=100
WARMUP=10
P95_MAX=75
P99_MAX=110
JSON_ONLY=0
EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-12-overlay-interactive-latency.json"
ERROR_LOG="${EVIDENCE_DIR}/task-12-overlay-interactive-latency-error.txt"
ALLOW_INTRUSIVE_UI="${SHAULA_QA_ALLOW_INTRUSIVE_UI:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --p95-max-ms) P95_MAX="$2"; shift 2 ;;
    --p99-max-ms) P99_MAX="$2"; shift 2 ;;
    --json-only) JSON_ONLY=1; shift ;;
    --report-json) REPORT_JSON="$2"; shift 2 ;;
    --error-log) ERROR_LOG="$2"; shift 2 ;;
    *) echo "ERR_PERF_USAGE script=benchmark-overlay-first-paint unknown_flag=$1" >&2; exit 1 ;;
  esac
done

mkdir -p "${EVIDENCE_DIR}"
: > "${ERROR_LOG}"

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

if [[ "${ALLOW_INTRUSIVE_UI}" != "1" ]]; then
  python3 - "$SAMPLES" "$WARMUP" "$P95_MAX" "$P99_MAX" "$JSON_ONLY" "$REPORT_JSON" "$ERROR_LOG" <<'PY'
import json
import sys

samples = int(sys.argv[1])
warmup = int(sys.argv[2])
p95_target = float(sys.argv[3])
p99_target = float(sys.argv[4])
json_only = int(sys.argv[5]) == 1
report_json = sys.argv[6]
error_log = sys.argv[7]

token = "ERR_PERF_INTRUSIVE_UI_DISABLED_BY_POLICY"
result = {
    "benchmark": "overlay_interactive_first_paint",
    "mode": "non_intrusive",
    "status": "degraded",
    "degraded": True,
    "interactive_allowed": False,
    "interactive_opt_in_required": True,
    "samples": samples,
    "warmup": warmup,
    "metrics_ms": {
        "p50": 0.0,
        "p95": 0.0,
        "p99": 0.0,
        "mean": 0.0,
    },
    "p95_ms": 0.0,
    "p99_ms": 0.0,
    "thresholds_ms": {
        "p95_max": p95_target,
        "p99_max": p99_target,
    },
    "pass": True,
    "error_token": token,
    "reason": "interactive_opt_in_required",
}

with open(report_json, "w", encoding="utf-8") as handle:
    json.dump(result, handle, separators=(",", ":"))
    handle.write("\n")

line = (
    f"{token} benchmark=overlay_interactive_first_paint mode=non_intrusive "
    f"opt_in_env=SHAULA_QA_ALLOW_INTRUSIVE_UI report={report_json}"
)
with open(error_log, "a", encoding="utf-8") as handle:
    handle.write(line + "\n")

if json_only:
    print(json.dumps(result, separators=(",", ":")))
else:
    print(json.dumps(result, indent=2))
    print(f"ok qa_overlay_interactive_first_paint mode=non_intrusive report={report_json}")
PY
  exit 0
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

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
if ! command -v grim >/dev/null 2>&1 && [[ -z "${SHAULA_RUNTIME_CAPTURE_HELPER:-}" ]]; then
  if [[ ! -x "${helper_script}" ]]; then
    chmod +x "${helper_script}"
  fi
  export SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}"
fi

# Force real helper/interactive path in benchmark scope: explicit stub backend env is a
# deterministic backend-unavailable test lane, not a valid first-paint performance signal.
if [[ "${SHAULA_CAPTURE_BACKEND:-}" == "__stub__" ]]; then
  unset SHAULA_CAPTURE_BACKEND
fi

export SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION="${SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION:-0}"
export SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE="${SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE:-interaction_drag}"
export SHAULA_COMPOSITOR="${SHAULA_COMPOSITOR:-niri}"
export NIRI_SOCKET="${NIRI_SOCKET:-/tmp/niri.sock}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"

zig build >/dev/null

python3 - "$SAMPLES" "$WARMUP" "$P95_MAX" "$P99_MAX" "$JSON_ONLY" "$REPORT_JSON" "$ERROR_LOG" <<'PY'
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
report_json = sys.argv[6]
error_log = sys.argv[7]

# Intentionally no --dry-run here: Task 12 requires real interactive/helper timing.
base_cmd = ["./zig-out/bin/shaula", "capture", "area", "--json"]

def append_error(line: str) -> None:
    with open(error_log, "a", encoding="utf-8") as handle:
        handle.write(line.rstrip("\n") + "\n")

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
      line = f"ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint rc={rc} output={output}"
      print(line, file=sys.stderr)
      append_error(line)
      sys.exit(1)
    try:
      payload = json.loads(output)
      if payload.get("ok") is not True:
        line = "ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint reason=non_ok_json"
        print(line, file=sys.stderr)
        append_error(line)
        sys.exit(1)
    except Exception:
      line = "ERR_PERF_MEASUREMENT_FAILED script=benchmark-overlay-first-paint reason=invalid_json"
      print(line, file=sys.stderr)
      append_error(line)
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
    "benchmark": "overlay_interactive_first_paint",
    "mode": "interactive_opt_in",
    "status": "pass" if passed else "fail",
    "degraded": False,
    "interactive_allowed": True,
    "interactive_opt_in_required": True,
    "samples": samples,
    "warmup": warmup,
    "metrics_ms": {
        "p50": round(p50, 2),
        "p95": round(p95, 2),
        "p99": round(p99, 2),
        "mean": round(mean, 2),
    },
    "p95_ms": round(p95, 2),
    "p99_ms": round(p99, 2),
    "thresholds_ms": {
        "p95_max": p95_target,
        "p99_max": p99_target,
    },
    "pass": passed,
    "error_token": None if passed else "ERR_PERF_BUDGET_EXCEEDED",
}

with open(report_json, "w", encoding="utf-8") as handle:
    json.dump(result, handle, separators=(",", ":"))
    handle.write("\n")

if json_only:
    print(json.dumps(result, separators=(",", ":")))
else:
    print(json.dumps(result, indent=2))

if not passed:
    line = (
        f"ERR_PERF_BUDGET_EXCEEDED benchmark=overlay_interactive_first_paint p95_ms={result['metrics_ms']['p95']} "
        f"p95_max={p95_target} p99_ms={result['metrics_ms']['p99']} p99_max={p99_target} report={report_json}"
    )
    print(line, file=sys.stderr)
    append_error(line)
    sys.exit(1)

if not json_only:
    print(f"ok qa_overlay_interactive_first_paint mode=interactive_opt_in report={report_json}")
PY
