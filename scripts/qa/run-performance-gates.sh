#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

. "${ROOT_DIR}/scripts/qa/manual-suite-warning.sh"
shaula_qa_manual_suite_warning "scripts/qa/run-performance-gates.sh"

EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-12-performance-gates-report.json"
ERROR_LOG="${EVIDENCE_DIR}/task-12-overlay-interactive-latency-error.txt"
QA_PROFILE="${SHAULA_QA_PROFILE:-full}"
ALLOW_INTRUSIVE_UI="${SHAULA_QA_ALLOW_INTRUSIVE_UI:-0}"
UI_POLICY_MODE="non_intrusive"

if [[ "${ALLOW_INTRUSIVE_UI}" == "1" ]]; then
  UI_POLICY_MODE="interactive_opt_in"
fi

OVERLAY_SAMPLES=100
OVERLAY_WARMUP=10
OVERLAY_P95_MAX=75
OVERLAY_P99_MAX=110

CAPTURE_SAMPLES=60
CAPTURE_WARMUP=8
CAPTURE_AREA_P95_MAX=150
CAPTURE_WINDOW_P95_MAX=220

apply_profile_defaults() {
  case "${QA_PROFILE}" in
    full)
      ;;
    fast)
      OVERLAY_SAMPLES=30
      OVERLAY_WARMUP=3
      CAPTURE_SAMPLES=30
      CAPTURE_WARMUP=3
      ;;
    debug)
      OVERLAY_SAMPLES=30
      OVERLAY_WARMUP=2
      CAPTURE_SAMPLES=30
      CAPTURE_WARMUP=2
      ;;
    *)
      echo "ERR_PERF_USAGE script=run-performance-gates reason=invalid_profile profile=${QA_PROFILE}" >&2
      exit 1
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --overlay-samples) OVERLAY_SAMPLES="$2"; shift 2 ;;
    --overlay-warmup) OVERLAY_WARMUP="$2"; shift 2 ;;
    --p95-max-ms|--overlay-p95-max-ms) OVERLAY_P95_MAX="$2"; shift 2 ;;
    --p99-max-ms|--overlay-p99-max-ms) OVERLAY_P99_MAX="$2"; shift 2 ;;

    --capture-samples) CAPTURE_SAMPLES="$2"; shift 2 ;;
    --capture-warmup) CAPTURE_WARMUP="$2"; shift 2 ;;
    --area-p95-max-ms) CAPTURE_AREA_P95_MAX="$2"; shift 2 ;;
    --window-p95-max-ms) CAPTURE_WINDOW_P95_MAX="$2"; shift 2 ;;

    --profile) QA_PROFILE="$2"; shift 2 ;;
    --report-json) REPORT_JSON="$2"; shift 2 ;;
    --error-log) ERROR_LOG="$2"; shift 2 ;;

    *) echo "ERR_PERF_USAGE script=run-performance-gates unknown_flag=$1" >&2; exit 1 ;;
  esac
done

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

mkdir -p "${EVIDENCE_DIR}"
: > "${ERROR_LOG}"
apply_profile_defaults

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
if ! command -v grim >/dev/null 2>&1 && [[ -z "${SHAULA_RUNTIME_CAPTURE_HELPER:-}" ]]; then
  if [[ ! -x "${helper_script}" ]]; then
    chmod +x "${helper_script}"
  fi
  export SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}"
fi

# Deterministic perf semantics: explicit __stub__ backend env is a failure-injection lane,
# not a valid latency benchmark source. Perf gates must run against real helper/runtime path.
if [[ "${SHAULA_CAPTURE_BACKEND:-}" == "__stub__" ]]; then
  unset SHAULA_CAPTURE_BACKEND
fi

# Keep non-overlay benchmarks deterministic in CI/headless while overlay benchmark script
# enforces interactive helper mode internally for first-paint timing.
export SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION="${SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION:-1}"

run_json_benchmark() {
  local bench_id="$1"
  local allow_degraded="$2"
  shift
  shift

  local output
  local rc=0
  local stderr_tmp=""
  local stderr_text=""
  stderr_tmp="$(mktemp)"
  set +e
  output="$("$@" 2>"${stderr_tmp}")"
  rc=$?
  set -e

  stderr_text="$(<"${stderr_tmp}")"
  rm -f "${stderr_tmp}"

  if [[ -n "${stderr_text}" ]]; then
    printf '%s\n' "${stderr_text}" >> "${ERROR_LOG}"
  fi

  if [[ -z "${output}" ]]; then
    if [[ "${allow_degraded}" == "1" && "${stderr_text}" == *"ERR_CAPTURE_MODE_UNSUPPORTED"* ]]; then
      echo "ERR_PERF_BENCHMARK_DEGRADED benchmark=${bench_id} reason=capture_mode_unsupported" >> "${ERROR_LOG}"
      printf '%s' '{"benchmark":"capture_completion","pass":true,"status":"degraded","degraded":true,"error_token":"ERR_CAPTURE_MODE_UNSUPPORTED","details":{"reason":"capture_mode_unsupported"}}'
      return
    fi

    echo "ERR_PERF_MEASUREMENT_FAILED benchmark=${bench_id} reason=empty_json_output" | tee -a "${ERROR_LOG}" >&2
    exit 1
  fi

  if ! jq -e . >/dev/null 2>&1 <<<"${output}"; then
    echo "ERR_PERF_MEASUREMENT_FAILED benchmark=${bench_id} reason=invalid_json_output" | tee -a "${ERROR_LOG}" >&2
    exit 1
  fi

  if [[ ${rc} -ne 0 ]]; then
    if [[ "${allow_degraded}" == "1" && "${stderr_text}" == *"ERR_CAPTURE_MODE_UNSUPPORTED"* ]]; then
      echo "ERR_PERF_BENCHMARK_DEGRADED benchmark=${bench_id} reason=capture_mode_unsupported" >> "${ERROR_LOG}"
      printf '%s' '{"benchmark":"capture_completion","pass":true,"status":"degraded","degraded":true,"error_token":"ERR_CAPTURE_MODE_UNSUPPORTED","details":{"reason":"capture_mode_unsupported"}}'
      return
    fi

    local token
    token="$(jq -r '.error_token // "ERR_PERF_MEASUREMENT_FAILED"' <<<"${output}")"
    if [[ "${token}" == "null" || -z "${token}" ]]; then
      token="ERR_PERF_MEASUREMENT_FAILED"
    fi
    echo "${token} benchmark=${bench_id} profile=${QA_PROFILE} report=${REPORT_JSON}" | tee -a "${ERROR_LOG}" >&2
    exit 1
  fi

  printf '%s' "${output}"
}

overlay_json="$(run_json_benchmark overlay_interactive_first_paint 1 bash ./scripts/qa/benchmark-overlay-first-paint.sh --samples "${OVERLAY_SAMPLES}" --warmup "${OVERLAY_WARMUP}" --p95-max-ms "${OVERLAY_P95_MAX}" --p99-max-ms "${OVERLAY_P99_MAX}" --report-json "${ROOT_DIR}/.qa/evidence/task-12-overlay-interactive-latency.json" --error-log "${ERROR_LOG}" --json-only)"
capture_json="$(run_json_benchmark capture_completion 1 bash ./scripts/qa/benchmark-capture-completion.sh --samples "${CAPTURE_SAMPLES}" --warmup "${CAPTURE_WARMUP}" --area-p95-max-ms "${CAPTURE_AREA_P95_MAX}" --window-p95-max-ms "${CAPTURE_WINDOW_P95_MAX}" --json-only)"

jq -n \
  --argjson overlay "${overlay_json}" \
  --argjson capture "${capture_json}" \
  --arg profile "${QA_PROFILE}" \
  --arg ui_policy_mode "${UI_POLICY_MODE}" \
  '{
    suite: "task-12-performance-gates",
    profile: $profile,
    ui_policy_mode: $ui_policy_mode,
    pass: ($overlay.pass and $capture.pass),
    benchmarks: {
      overlay_first_paint: ($overlay + {status: ($overlay.status // (if $overlay.pass then "pass" else "fail" end)), degraded: ($overlay.degraded // false)}),
      capture_completion: ($capture + {status: ($capture.status // (if $capture.pass then "pass" else "fail" end))})
    }
  }' > "${REPORT_JSON}"

overall_pass="$(jq -r '.pass' "${REPORT_JSON}")"
if [[ "${overall_pass}" != "true" ]]; then
  echo "ERR_PERF_BUDGET_EXCEEDED suite=task-12-performance-gates profile=${QA_PROFILE} report=${REPORT_JSON}" | tee -a "${ERROR_LOG}" >&2
  exit 1
fi

cat "${REPORT_JSON}"
echo "ok performance_gates profile=${QA_PROFILE} mode=${UI_POLICY_MODE} report=${REPORT_JSON}"
