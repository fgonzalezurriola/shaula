#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
ERROR_TXT="${EVIDENCE_DIR}/task-16-full-regression-error.txt"

mkdir -p "${EVIDENCE_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
if ! command -v grim >/dev/null 2>&1 && [[ -z "${SHAULA_RUNTIME_CAPTURE_HELPER:-}" ]]; then
  if [[ ! -x "${helper_script}" ]]; then
    chmod +x "${helper_script}"
  fi
  export SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}"
fi

export SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1
QA_PROFILE="${SHAULA_QA_PROFILE:-full}"
KEEP_ARTIFACTS="${QA_KEEP_ARTIFACTS:-0}"
ALLOW_INTRUSIVE_UI="${SHAULA_QA_ALLOW_INTRUSIVE_UI:-0}"
UI_POLICY_MODE="non_intrusive"
REPORT_JSON="${EVIDENCE_DIR}/task-16-full-regression.json"
if [[ "${ALLOW_INTRUSIVE_UI}" == "1" ]]; then
  UI_POLICY_MODE="interactive_opt_in"
fi
if [[ "${QA_PROFILE}" == "debug" ]]; then
  REPORT_JSON="${EVIDENCE_DIR}/task-16-debug-regression.json"
fi
RUN_TS="$(date -u +"%Y%m%dT%H%M%SZ")"
RUN_DIR="/tmp/shaula/runs/${RUN_TS}-all-${QA_PROFILE}"
mkdir -p "${RUN_DIR}"
ln -sfn "${RUN_DIR}" /tmp/shaula/runs/latest

cleanup_qa_artifacts() {
  if [[ "${KEEP_ARTIFACTS}" == "1" ]]; then
    return
  fi

  rm -f /tmp/shaula/task10-*.png \
        /tmp/shaula/task17-e2e-*.png \
        /tmp/shaula/task9-*.png \
        /tmp/shaula/task8-*.png \
        /tmp/shaula/task8-*.token \
        /tmp/shaula/task6-history-topn/capture-*.png \
        /tmp/shaula/task2-capability-*.png \
        /tmp/shaula/task3-capture-content-fullscreen.png \
        /tmp/shaula/task3-stub-signature-1x1.png \
        /tmp/shaula/qa-runtime-capture.png 2>/dev/null || true
  rmdir /tmp/shaula/task6-history-topn 2>/dev/null || true
}
trap cleanup_qa_artifacts EXIT

set +e
negative_preflight_output="$(env -u WAYLAND_DISPLAY -u NIRI_SOCKET bash ./scripts/qa/preflight-wayland-niri.sh 2>&1)"
negative_preflight_rc=$?
set -e

if [[ ${negative_preflight_rc} -eq 0 ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=negative_preflight_unexpected_success" > "${ERROR_TXT}"
else
  printf '%s\n' "${negative_preflight_output}" > "${ERROR_TXT}"
  grep -q 'ERR_PREFLIGHT_ENV_NOT_READY' "${ERROR_TXT}" || {
    echo "ERR_PREFLIGHT_ENV_NOT_READY reason=negative_preflight_token_missing" >> "${ERROR_TXT}"
    exit 1
  }
fi

bash ./scripts/qa/run-unit-tests.sh
SHAULA_QA_PROFILE="${QA_PROFILE}" QA_KEEP_ARTIFACTS="${KEEP_ARTIFACTS}" SHAULA_QA_ALLOW_INTRUSIVE_UI="${ALLOW_INTRUSIVE_UI}" bash ./scripts/qa/run-integration-tests.sh
SHAULA_QA_PROFILE="${QA_PROFILE}" QA_KEEP_ARTIFACTS="${KEEP_ARTIFACTS}" SHAULA_QA_ALLOW_INTRUSIVE_UI="${ALLOW_INTRUSIVE_UI}" bash ./scripts/qa/run-e2e-niri.sh
SHAULA_QA_PROFILE="${QA_PROFILE}" SHAULA_QA_ALLOW_INTRUSIVE_UI="${ALLOW_INTRUSIVE_UI}" bash ./scripts/qa/run-performance-gates.sh >/dev/null
bash ./scripts/qa/release-readiness-capture-fix.sh >/dev/null

integration_layer_report="${EVIDENCE_DIR}/task-11-layer-integration-report.json"
e2e_layer_report="${EVIDENCE_DIR}/task-11-layer-e2e-niri-report.json"
perf_layer_report="${EVIDENCE_DIR}/task-12-performance-gates-report.json"
release_layer_report="${EVIDENCE_DIR}/task-15-release-readiness.json"

if [[ ! -f "${integration_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=integration_layer_report_missing path=${integration_layer_report}" >&2
  exit 1
fi

if [[ ! -f "${e2e_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=e2e_layer_report_missing path=${e2e_layer_report}" >&2
  exit 1
fi

if [[ ! -f "${perf_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=perf_layer_report_missing path=${perf_layer_report}" >&2
  exit 1
fi

if [[ ! -f "${release_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=release_layer_report_missing path=${release_layer_report}" >&2
  exit 1
fi

integration_layer_pass="$(jq -r '.pass' "${integration_layer_report}")"
e2e_layer_pass="$(jq -r '.pass' "${e2e_layer_report}")"
perf_layer_pass="$(jq -r '.pass' "${perf_layer_report}")"
release_layer_pass="$(jq -r '.ready' "${release_layer_report}")"

if [[ "${integration_layer_pass}" != "true" || "${e2e_layer_pass}" != "true" || "${perf_layer_pass}" != "true" || "${release_layer_pass}" != "true" ]]; then
  echo "QA failure summary (layer): integration_pass=${integration_layer_pass} e2e_pass=${e2e_layer_pass} perf_pass=${perf_layer_pass} release_ready=${release_layer_pass}" >&2
  echo "- integration log: ${EVIDENCE_DIR}/task-11-layer-integration-report-error.txt" >&2
  echo "- e2e log: ${EVIDENCE_DIR}/task-11-layer-e2e-niri-report-error.txt" >&2
  echo "- perf log: ${EVIDENCE_DIR}/task-12-overlay-interactive-latency-error.txt" >&2
  echo "- release log: ${EVIDENCE_DIR}/task-15-release-readiness-error.txt" >&2
  echo "- matrix error file: ${ERROR_TXT}" >&2
  echo "ERR_QA_MATRIX_INVALID reason=layer_report_failures integration_pass=${integration_layer_pass} e2e_pass=${e2e_layer_pass} perf_pass=${perf_layer_pass} release_ready=${release_layer_pass}" >&2
  exit 1
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

integration_subchecks_json="$(jq -c '.subchecks' "${integration_layer_report}")"
e2e_subchecks_json="$(jq -c '.subchecks' "${e2e_layer_report}")"
perf_subchecks_json="$(jq -c '[.benchmarks.overlay_first_paint, .benchmarks.capture_completion, .benchmarks.daemon_idle_footprint] | map({id:("performance." + .benchmark), pass:.pass, status:(.status // (if .pass then "pass" else "fail" end))})' "${perf_layer_report}")"
release_subchecks_json="$(jq -c '.checks | map({id:("release." + .id), pass:.pass})' "${release_layer_report}")"

matrix_json="$(jq -c -n \
  --argjson integration "${integration_subchecks_json}" \
  --argjson e2e "${e2e_subchecks_json}" \
  --argjson perf "${perf_subchecks_json}" \
  --argjson release "${release_subchecks_json}" \
  '
    [
      { id: "unit.preflight.schema", pass: true },
      { id: "unit.errors.matrix", pass: true },
      { id: "unit.exit_code.mapping", pass: true }
    ] + $integration + $e2e + $perf + $release
  ' \
)"

overall_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${matrix_json}" >/dev/null && echo true || echo false)"

jq -n \
  --arg timestamp "${timestamp}" \
  --arg profile "${QA_PROFILE}" \
  --arg ui_policy_mode "${UI_POLICY_MODE}" \
  --argjson integration_subchecks "${integration_subchecks_json}" \
  --argjson e2e_subchecks "${e2e_subchecks_json}" \
  --argjson perf_subchecks "${perf_subchecks_json}" \
  --argjson release_subchecks "${release_subchecks_json}" \
  --argjson matrix "${matrix_json}" \
  --argjson overall_pass "${overall_pass}" \
  '{
    suite: "task-16-full-regression",
    timestamp: $timestamp,
    profile: $profile,
    pass: $overall_pass,
    ui_policy_mode: $ui_policy_mode,
    layers: {
      unit: { pass: true, script: "scripts/qa/run-unit-tests.sh" },
      integration: {
        pass: true,
        script: "scripts/qa/run-integration-tests.sh",
        subchecks: $integration_subchecks
      },
      e2e_niri: {
        pass: true,
        script: "scripts/qa/run-e2e-niri.sh",
        subchecks: $e2e_subchecks
      },
      performance: {
        pass: true,
        script: "scripts/qa/run-performance-gates.sh",
        subchecks: $perf_subchecks
      },
      release_readiness: {
        pass: true,
        script: "scripts/qa/release-readiness-capture-fix.sh",
        subchecks: $release_subchecks
      }
    },
    matrix: $matrix
  }' > "${REPORT_JSON}"

if [[ "${QA_PROFILE}" == "debug" ]]; then
  printf 'profile=%s\nkeep_artifacts=%s\nrun_dir=%s\nreport=%s\n' \
    "${QA_PROFILE}" \
    "${KEEP_ARTIFACTS}" \
    "${RUN_DIR}" \
    "${REPORT_JSON}" > "${EVIDENCE_DIR}/task-16-debug-regression.txt"
fi

if [[ "${overall_pass}" != "true" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=matrix_has_failed_subchecks report=${REPORT_JSON}" >&2
  exit 1
fi

echo "ok qa_all_tests profile=${QA_PROFILE} keep_artifacts=${KEEP_ARTIFACTS} run_dir=${RUN_DIR} mode=${UI_POLICY_MODE} report=${REPORT_JSON}"
