#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

SAMPLES=60
WARMUP=8
MAX_ADDED_P95_MS=20
SOCKET="${SHAULA_SOCKET:-/tmp/shaula-task9-overhead.sock}"
JSON_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --max-added-p95-ms) MAX_ADDED_P95_MS="$2"; shift 2 ;;
    --socket) SOCKET="$2"; shift 2 ;;
    --json-only) JSON_ONLY=1; shift ;;
    *)
      echo "ERR_PLUGIN_BENCH_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

if [[ ! "${SAMPLES}" =~ ^[0-9]+$ ]] || [[ ! "${WARMUP}" =~ ^[0-9]+$ ]]; then
  echo "ERR_PLUGIN_BENCH_USAGE reason=non_numeric_samples_or_warmup samples=${SAMPLES} warmup=${WARMUP}" >&2
  exit 1
fi

if [[ ! "${MAX_ADDED_P95_MS}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "ERR_PLUGIN_BENCH_USAGE reason=non_numeric_threshold value=${MAX_ADDED_P95_MS}" >&2
  exit 1
fi

if (( SAMPLES < 30 )); then
  echo "ERR_PLUGIN_BENCH_USAGE reason=samples_below_minimum min=30 actual=${SAMPLES}" >&2
  exit 1
fi

if (( WARMUP < 0 )); then
  echo "ERR_PLUGIN_BENCH_USAGE reason=negative_warmup actual=${WARMUP}" >&2
  exit 1
fi

if [[ ! -x "./integrations/noctalia/noctalia-plugin-poc.sh" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-plugin-poc.sh" >&2
  exit 1
fi

if [[ ! -x "./integrations/noctalia/noctalia-action-adapter.sh" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-action-adapter.sh" >&2
  exit 1
fi

zig build >/dev/null

python3 - "${SAMPLES}" "${WARMUP}" "${MAX_ADDED_P95_MS}" "${JSON_ONLY}" "${ROOT_DIR}" <<'PY'
import json
import math
import os
import subprocess
import sys
import time

samples = int(sys.argv[1])
warmup = int(sys.argv[2])
max_added_p95 = float(sys.argv[3])
json_only = int(sys.argv[4]) == 1
root_dir = sys.argv[5]
shaula_bin = os.path.join(root_dir, "zig-out", "bin", "shaula")
adapter_script = os.path.join(root_dir, "integrations", "noctalia", "noctalia-action-adapter.sh")
helper_script = os.path.join(root_dir, "scripts", "qa", "fake_runtime_capture_helper.sh")

env = os.environ.copy()
env.setdefault("SHAULA_RUNTIME_CAPTURE_HELPER", helper_script)
env.setdefault("SHAULA_COMPOSITOR", "niri")
env.setdefault("NIRI_SOCKET", "/tmp/niri.sock")
env.setdefault("WAYLAND_DISPLAY", "wayland-1")

def pctl(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    rank = math.ceil((p / 100.0) * len(sorted_vals)) - 1
    idx = min(max(rank, 0), len(sorted_vals) - 1)
    return sorted_vals[idx]

def run_subprocess(args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        args,
        cwd=root_dir,
        env=env,
        text=True,
        capture_output=True,
        timeout=8,
        check=False,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, output.strip()


def parse_json(output: str, context: str) -> dict:
    try:
        return json.loads(output)
    except Exception:
        print(f"ERR_PLUGIN_BENCH_EXEC_FAILED benchmark={context} reason=invalid_json output={output!r}", file=sys.stderr)
        sys.exit(1)

def run_baseline(iteration: int) -> float:
    t0 = time.perf_counter()
    rc, output = run_subprocess([shaula_bin, "capture", "fullscreen", "--json"])
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    payload = parse_json(output, "baseline")
    if rc != 0 or payload.get("ok") is not True or payload.get("mode") != "fullscreen":
        print("ERR_PLUGIN_BENCH_EXEC_FAILED benchmark=baseline reason=non_ok_capture", file=sys.stderr)
        sys.exit(1)
    return elapsed_ms

def run_plugin(iteration: int) -> float:
    t0 = time.perf_counter()
    rc, output = run_subprocess([
        "bash",
        adapter_script,
        "--action",
        "capture-fullscreen",
        "--execute",
        "--request-id",
        f"bench-plugin-{iteration}",
        "--shaula-bin",
        shaula_bin,
    ])
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    payload = parse_json(output, "plugin")
    if rc != 0 or payload.get("ok") is not True:
        print("ERR_PLUGIN_BENCH_EXEC_FAILED benchmark=plugin reason=adapter_failed", file=sys.stderr)
        sys.exit(1)
    action = payload.get("action") or {}
    execution = payload.get("execution") or {}
    if action.get("id") != "capture-fullscreen" or execution.get("ok") is not True:
        print("ERR_PLUGIN_BENCH_EXEC_FAILED benchmark=plugin reason=invalid_adapter_contract", file=sys.stderr)
        sys.exit(1)
    return elapsed_ms

for i in range(warmup):
    run_baseline(i)
    run_plugin(i)

baseline = [run_baseline(i) for i in range(samples)]
plugin = [run_plugin(i) for i in range(samples)]

baseline.sort()
plugin.sort()

baseline_p95 = pctl(baseline, 95)
plugin_p95 = pctl(plugin, 95)
added_p95 = plugin_p95 - baseline_p95

passed = added_p95 <= max_added_p95
result = {
    "benchmark": "noctalia_optional_plugin_overhead",
    "samples": samples,
    "warmup": warmup,
    "adapter": "integrations/noctalia/noctalia-action-adapter.sh",
    "metrics_ms": {
        "baseline": {"p95": round(baseline_p95, 2)},
        "plugin": {"p95": round(plugin_p95, 2)},
        "added": {"p95": round(added_p95, 2)},
    },
    "thresholds_ms": {"max_added_p95": max_added_p95},
    "pass": passed,
    "error_token": None if passed else "ERR_PLUGIN_OVERHEAD_BUDGET_EXCEEDED",
}

if json_only:
    print(json.dumps(result, separators=(",", ":")))
else:
    print(json.dumps(result, indent=2))

if not passed:
    print(
        f"ERR_PLUGIN_OVERHEAD_BUDGET_EXCEEDED benchmark=noctalia_optional_plugin_overhead added_p95_ms={result['metrics_ms']['added']['p95']} max_allowed_ms={max_added_p95}",
        file=sys.stderr,
    )
    sys.exit(1)
PY

echo "PASS_PLUGIN_OVERHEAD_BENCH samples=${SAMPLES} warmup=${WARMUP} max_added_p95_ms=${MAX_ADDED_P95_MS}" >&2
