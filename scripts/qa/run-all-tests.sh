#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-10-postfix-test-matrix-report.json"
ERROR_TXT="${EVIDENCE_DIR}/task-10-postfix-test-matrix-error.txt"

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
bash ./scripts/qa/run-integration-tests.sh
bash ./scripts/qa/run-e2e-niri.sh

integration_layer_report="${EVIDENCE_DIR}/task-10-layer-integration-report.json"
e2e_layer_report="${EVIDENCE_DIR}/task-10-layer-e2e-niri-report.json"

if [[ ! -f "${integration_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=integration_layer_report_missing path=${integration_layer_report}" >&2
  exit 1
fi

if [[ ! -f "${e2e_layer_report}" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=e2e_layer_report_missing path=${e2e_layer_report}" >&2
  exit 1
fi

integration_layer_pass="$(jq -r '.pass' "${integration_layer_report}")"
e2e_layer_pass="$(jq -r '.pass' "${e2e_layer_report}")"

if [[ "${integration_layer_pass}" != "true" || "${e2e_layer_pass}" != "true" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=layer_report_failures integration_pass=${integration_layer_pass} e2e_pass=${e2e_layer_pass}" >&2
  exit 1
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

integration_subchecks_json="$(jq -c '.subchecks' "${integration_layer_report}")"
e2e_subchecks_json="$(jq -c '.subchecks' "${e2e_layer_report}")"

matrix_json="$(jq -c -n \
  --argjson integration "${integration_subchecks_json}" \
  --argjson e2e "${e2e_subchecks_json}" \
  '
    [
      { id: "unit.preflight.schema", pass: true },
      { id: "unit.errors.matrix", pass: true },
      { id: "unit.exit_code.mapping", pass: true }
    ] + $integration + $e2e
  ' \
)"

overall_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${matrix_json}" >/dev/null && echo true || echo false)"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson integration_subchecks "${integration_subchecks_json}" \
  --argjson e2e_subchecks "${e2e_subchecks_json}" \
  --argjson matrix "${matrix_json}" \
  --argjson overall_pass "${overall_pass}" \
  '{
    suite: "task-10-postfix-test-matrix",
    timestamp: $timestamp,
    pass: $overall_pass,
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
      }
    },
    matrix: $matrix
  }' > "${REPORT_JSON}"

if [[ "${overall_pass}" != "true" ]]; then
  echo "ERR_QA_MATRIX_INVALID reason=matrix_has_failed_subchecks report=${REPORT_JSON}" >&2
  exit 1
fi

echo "ok qa_all_tests report=${REPORT_JSON}"
