#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

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

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-10-layer-integration-report.json"
ERROR_LOG="${EVIDENCE_DIR}/task-10-layer-integration-report-error.txt"

mkdir -p "${EVIDENCE_DIR}"
: > "${ERROR_LOG}"

subchecks_json='[]'

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

run_subcheck "integration.daemon.lifecycle" "bash ./scripts/qa/test-daemon-lifecycle.sh"
run_subcheck "integration.daemon.state_machine" "bash ./scripts/qa/assert-daemon-state-machine.sh"
run_subcheck "integration.capture.core_modes" "bash ./scripts/qa/test-capture-core-modes.sh"
run_subcheck "integration.capture.integrity.runtime_non_stub" "bash ./scripts/qa/assert-no-runtime-stub.sh"
run_subcheck "integration.capture.integrity.content_validity" "bash ./scripts/qa/assert-capture-content-validity.sh"
run_subcheck "integration.capture.capabilities.strict_contract" "bash ./scripts/qa/assert-capabilities-consistency.sh"
run_subcheck "integration.capture.output.default_path" "bash ./scripts/qa/assert-default-output-path.sh"
run_subcheck "integration.history.topn_20" "bash ./scripts/qa/assert-history-topn.sh --topn 20 --shots 25"
run_subcheck "integration.overlay.base_selection" "bash ./scripts/qa/assert-overlay-base-selection.sh"
run_subcheck "integration.capture.shell_artifact_guard" "bash ./scripts/qa/assert-shell-artifact-guard.sh --inject-known-marker"
run_subcheck "integration.capture.noctalia.panel_hide_handshake" "bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh"
run_subcheck "integration.noctalia.actions_mvp" "bash ./scripts/qa/assert-noctalia-actions.sh --with-plugin"
run_subcheck "integration.noctalia.optional_core_without_plugin" "bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin"
run_subcheck "integration.capture.result_schema" "bash ./scripts/qa/assert-capture-result-schema.sh"
run_subcheck "integration.pipeline.post_capture" "bash ./scripts/qa/test-post-capture-pipeline.sh"
run_subcheck "integration.history.consistency" "bash ./scripts/qa/test-history-consistency.sh"

layer_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${subchecks_json}" >/dev/null && echo true || echo false)"
timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson pass "${layer_pass}" \
  --argjson subchecks "${subchecks_json}" \
  '{
    suite: "task-10-layer-integration",
    timestamp: $timestamp,
    pass: $pass,
    script: "scripts/qa/run-integration-tests.sh",
    subchecks: $subchecks
  }' > "${REPORT_JSON}"

if [[ "${layer_pass}" != "true" ]]; then
  echo "ERR_QA_INTEGRATION_FAILED report=${REPORT_JSON} log=${ERROR_LOG}" >&2
  exit 1
fi

echo "ok qa_integration_tests"
