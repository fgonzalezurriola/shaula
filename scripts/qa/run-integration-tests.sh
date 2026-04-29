#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
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
if [[ "${ALLOW_INTRUSIVE_UI}" == "1" ]]; then
  UI_POLICY_MODE="interactive_opt_in"
fi
RUN_TS="$(date -u +"%Y%m%dT%H%M%SZ")"
RUN_DIR="/tmp/shaula/runs/${RUN_TS}-integration-${QA_PROFILE}"
mkdir -p "${RUN_DIR}"
ln -sfn "${RUN_DIR}" /tmp/shaula/runs/latest

EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-11-layer-integration-report.json"
ERROR_LOG="${EVIDENCE_DIR}/task-11-layer-integration-report-error.txt"

mkdir -p "${EVIDENCE_DIR}"
: > "${ERROR_LOG}"

cleanup_qa_artifacts() {
  if [[ "${KEEP_ARTIFACTS}" == "1" ]]; then
    return
  fi

  rm -f /tmp/shaula/task10-*.png \
        /tmp/shaula/task9-*.png \
        /tmp/shaula/task8-*.png \
        /tmp/shaula/task8-*.token \
        /tmp/shaula/task6-history-topn/capture-*.png \
        /tmp/shaula/qa-runtime-capture.png \
        /tmp/shaula/task2-capability-*.png \
        /tmp/shaula/task3-capture-content-fullscreen.png \
        /tmp/shaula/task3-stub-signature-1x1.png \
        /tmp/shaula/test-runtime-*.png 2>/dev/null || true
  rmdir /tmp/shaula/task6-history-topn 2>/dev/null || true
}
trap cleanup_qa_artifacts EXIT

subchecks_json='[]'
failed_checks='[]'

run_subcheck() {
  local id="$1"
  local cmd="$2"
  local output=""
  local rc=0
  local pass=false

  set +e
  output="$(bash -lc "${cmd}" 2>&1)"
  rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    pass=true
  else
    failed_checks="$(jq -c -n \
      --argjson current "${failed_checks}" \
      --arg id "${id}" \
      --arg cmd "${cmd}" \
      '$current + [{ id: $id, command: $cmd }]' \
    )"
  fi

  printf '[%s] rc=%s cmd=%s\n%s\n\n' "${id}" "${rc}" "${cmd}" "${output}" >> "${ERROR_LOG}"

  subchecks_json="$(jq -c -n \
    --argjson current "${subchecks_json}" \
    --arg id "${id}" \
    --arg cmd "${cmd}" \
    --arg output "${output}" \
    --argjson pass "${pass}" \
    '$current + [{ id: $id, pass: $pass, command: $cmd, output: $output }]' \
  )"
}

record_degraded_subcheck() {
  local id="$1"
  local cmd="$2"
  local token="$3"
  local reason="$4"
  local output="${token} reason=${reason} mode=${UI_POLICY_MODE} opt_in_env=SHAULA_QA_ALLOW_INTRUSIVE_UI"

  printf '[%s] rc=0 status=degraded cmd=%s\n%s\n\n' "${id}" "${cmd}" "${output}" >> "${ERROR_LOG}"

  subchecks_json="$(jq -c -n \
    --argjson current "${subchecks_json}" \
    --arg id "${id}" \
    --arg cmd "${cmd}" \
    --arg output "${output}" \
    --arg token "${token}" \
    --arg reason "${reason}" \
    --arg mode "${UI_POLICY_MODE}" \
    '$current + [{ id: $id, status: "degraded", pass: true, command: $cmd, output: $output, error_token: $token, reason: $reason, mode: $mode }]' \
  )"
}

run_subcheck "integration.daemon.lifecycle" "bash ./scripts/qa/test-daemon-lifecycle.sh"
run_subcheck "integration.daemon.state_machine" "bash ./scripts/qa/assert-daemon-state-machine.sh"
run_subcheck "integration.capture.core_modes" "bash ./scripts/qa/test-capture-core-modes.sh"
run_subcheck "integration.capture.integrity.runtime_non_stub" "bash ./scripts/qa/assert-no-runtime-stub.sh"
run_subcheck "integration.capture.integrity.content_validity" "bash ./scripts/qa/assert-capture-content-validity.sh"
run_subcheck "integration.capture.capabilities.strict_contract" "bash ./scripts/qa/assert-capabilities-consistency.sh"
run_subcheck "integration.capture.output.default_path" "bash ./scripts/qa/assert-default-output-path.sh"
run_subcheck "integration.history.topn_20" "bash ./scripts/qa/assert-history-topn.sh --topn 20 --shots 25"

if [[ "${QA_PROFILE}" == "full" || "${QA_PROFILE}" == "debug" ]]; then
  run_subcheck "integration.overlay.base_selection" "bash ./scripts/qa/assert-overlay-base-selection.sh"
  run_subcheck "integration.overlay.helper.contract_ok" "bash ./scripts/qa/test-overlay-helper-contract-ok.sh"
  run_subcheck "integration.overlay.helper.contract_malformed" "bash ./scripts/qa/test-overlay-helper-contract-malformed.sh"
  run_subcheck "integration.overlay.geometry.fixtures" "bash ./scripts/qa/assert-overlay-geometry-fixtures.sh"
  if [[ "${ALLOW_INTRUSIVE_UI}" == "1" ]]; then
    run_subcheck "integration.overlay.helper.interactive_lanes" "bash ./scripts/qa/assert-overlay-helper-interactive.sh"
  else
    record_degraded_subcheck "integration.overlay.helper.interactive_lanes" "bash ./scripts/qa/assert-overlay-helper-interactive.sh" "ERR_QA_INTRUSIVE_UI_DISABLED_BY_POLICY" "interactive_opt_in_required"
  fi
  run_subcheck "integration.capture.shell_artifact_guard" "bash ./scripts/qa/assert-shell-artifact-guard.sh --inject-known-marker"
  run_subcheck "integration.capture.noctalia.panel_hide_handshake" "bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh"
  run_subcheck "integration.noctalia.actions_mvp" "bash ./scripts/qa/assert-noctalia-actions.sh --with-plugin"
  run_subcheck "integration.noctalia.optional_core_without_plugin" "bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin"
  run_subcheck "integration.capture.result_schema" "bash ./scripts/qa/assert-capture-result-schema.sh"
  run_subcheck "integration.pipeline.post_capture" "bash ./scripts/qa/test-post-capture-pipeline.sh"
  run_subcheck "integration.history.consistency" "bash ./scripts/qa/test-history-consistency.sh"
else
  run_subcheck "integration.capture.result_schema" "bash ./scripts/qa/assert-capture-result-schema.sh"
  run_subcheck "integration.history.consistency" "bash ./scripts/qa/test-history-consistency.sh"
fi

layer_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${subchecks_json}" >/dev/null && echo true || echo false)"
timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson pass "${layer_pass}" \
  --arg ui_policy_mode "${UI_POLICY_MODE}" \
  --argjson subchecks "${subchecks_json}" \
  '{
    suite: "task-11-layer-integration",
    timestamp: $timestamp,
    pass: $pass,
    ui_policy_mode: $ui_policy_mode,
    script: "scripts/qa/run-integration-tests.sh",
    subchecks: $subchecks
  }' > "${REPORT_JSON}"

if [[ "${layer_pass}" != "true" ]]; then
  echo "QA failure summary (integration):" >&2
  jq -r '.[] | "- \(.id): \(.command)"' <<<"${failed_checks}" >&2
  echo "ERR_QA_INTEGRATION_FAILED report=${REPORT_JSON} log=${ERROR_LOG}" >&2
  exit 1
fi

echo "ok qa_integration_tests profile=${QA_PROFILE} keep_artifacts=${KEEP_ARTIFACTS} run_dir=${RUN_DIR} mode=${UI_POLICY_MODE}"
