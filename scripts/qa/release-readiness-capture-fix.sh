#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-15-release-readiness.json"
ERROR_TXT="${EVIDENCE_DIR}/task-15-release-readiness-error.txt"

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

if [[ ! -x "${ROOT_DIR}/zig-out/bin/shaula" ]]; then
  zig build >/dev/null
fi

checks_json='[]'
blockers_json='[]'

append_blocker() {
  local blocker="$1"
  blockers_json="$(jq -c -n \
    --argjson current "${blockers_json}" \
    --arg blocker "${blocker}" \
    '$current + [$blocker]' \
  )"
}

append_check() {
  local id="$1"
  local pass="$2"
  local detail="$3"

  checks_json="$(jq -c -n \
    --argjson current "${checks_json}" \
    --arg id "${id}" \
    --argjson pass "${pass}" \
    --arg detail "${detail}" \
    '$current + [{ id: $id, pass: $pass, detail: $detail }]' \
  )"

  if [[ "${pass}" != "true" ]]; then
    append_blocker "${id}: ${detail}"
  fi
}

check_json_file() {
  local id="$1"
  local path="$2"
  local expr="$3"

  if [[ ! -f "${path}" ]]; then
    append_check "${id}" false "missing_file path=${path}"
    return
  fi

  if jq -e "${expr}" "${path}" >/dev/null 2>&1; then
    append_check "${id}" true "pass"
  else
    append_check "${id}" false "invalid_content path=${path}"
  fi
}

matrix_report="${EVIDENCE_DIR}/task-10-postfix-test-matrix-report.json"
if [[ ! -f "${matrix_report}" && -f "${EVIDENCE_DIR}/task-16-full-regression.json" ]]; then
  matrix_report="${EVIDENCE_DIR}/task-16-full-regression.json"
fi

check_json_file \
  "evidence.matrix.consolidated" \
  "${matrix_report}" \
  '.pass == true and .layers.integration.pass == true and .layers.e2e_niri.pass == true and .layers.performance.pass == true'

required_matrix_ids=(
  "unit.errors.matrix"
  "unit.exit_code.mapping"
  "integration.capture.integrity.runtime_non_stub"
  "integration.capture.integrity.content_validity"
  "integration.capture.capabilities.strict_contract"
  "integration.capture.output.default_path"
  "integration.history.topn_20"
  "integration.overlay.base_selection"
  "integration.overlay.helper.contract_ok"
  "integration.overlay.helper.contract_malformed"
  "integration.overlay.helper.interactive_lanes"
  "integration.overlay.geometry.fixtures"
  "integration.capture.shell_artifact_guard"
  "integration.noctalia.optional_core_without_plugin"
  "e2e.capture.capabilities.strict_contract"
  "e2e.failure.unsupported_compositor"
  "e2e.failure.permission_clipboard"
)

for matrix_id in "${required_matrix_ids[@]}"; do
  check_id="matrix.subcheck.${matrix_id}"

  if [[ ! -f "${matrix_report}" ]]; then
    append_check "${check_id}" false "missing_file path=${matrix_report}"
    continue
  fi

  if jq -e --arg id "${matrix_id}" '.matrix | any(.id == $id and .pass == true)' "${matrix_report}" >/dev/null 2>&1; then
    append_check "${check_id}" true "pass"
  else
    append_check "${check_id}" false "missing_or_failed_subcheck id=${matrix_id}"
  fi
done

check_json_file \
  "evidence.task3.capture_content_validity" \
  "${EVIDENCE_DIR}/task-3-capture-content-validity.json" \
  '.pass == true and .checks.png_decode.pass == true and .checks.stub_signature_rejected.pass == true and .checks.dimensions_match.pass == true'

check_json_file \
  "evidence.task8.shell_artifact_guard" \
  "${EVIDENCE_DIR}/task-8-shell-artifact-guard.json" \
  '.pass == true and .checks.handshake_warning_present.pass == true and .checks.panel_marker_absent.pass == true'

check_json_file \
  "evidence.task9.noctalia_actions" \
  "${EVIDENCE_DIR}/task-9-noctalia-actions.json" \
  '.pass == true and .plugin_optional == true and .adapter_deterministic == true and .checks.execute_capture_window_expected_unsupported.pass == true'

check_json_file \
  "evidence.task10.open_folder" \
  "${EVIDENCE_DIR}/task-10-open-folder.json" \
  '.pass == true and .checks.action_non_fatal.pass == true and .checks.capture_remains_successful.pass == true'

check_json_file \
  "evidence.task10.open_clipboard_error" \
  "${EVIDENCE_DIR}/task-10-open-clipboard-error.json" \
  '.pass == true and .checks.action_non_fatal.pass == true and .checks.capture_remains_successful.pass == true'

check_json_file \
  "evidence.task13.multioutput_geometry" \
  "${EVIDENCE_DIR}/task-13-multioutput-geometry.json" \
  '.pass == true and .checks.fixture_shape_valid == true and .checks.runtime_arg_format_valid == true and .checks.zig_build_test_pass == true'

check_json_file \
  "evidence.task13.fractional_scaling" \
  "${EVIDENCE_DIR}/task-13-fractional-scaling.json" \
  '.pass == true and .checks.non_negative_dimensions == true and .checks.runtime_arg_format_valid == true and .checks.zig_build_test_pass == true'

overlay_evidence="${EVIDENCE_DIR}/task-7-overlay-base.txt"
check_json_file \
  "evidence.task7.overlay_base" \
  "${overlay_evidence}" \
  '.ok == true and .selection.cancelled == false and .selection.geometry.width > 0 and .selection.geometry.height > 0'

history_evidence="${EVIDENCE_DIR}/task-6-history-topn.txt"
if [[ ! -f "${history_evidence}" ]]; then
  append_check "evidence.task6.history_topn" false "missing_file path=${history_evidence}"
else
  if grep -q '^topn=20$' "${history_evidence}" && grep -q '^ordered_paths=true$' "${history_evidence}"; then
    append_check "evidence.task6.history_topn" true "pass"
  else
    append_check "evidence.task6.history_topn" false "invalid_content path=${history_evidence}"
  fi
fi

if [[ "${SHAULA_RELEASE_READINESS_FORCE_BLOCK:-0}" == "1" ]]; then
  append_check "guard.force_block" false "forced_by_env SHAULA_RELEASE_READINESS_FORCE_BLOCK=1"
else
  append_check "guard.force_block" true "pass"
fi

capabilities_json="$(./zig-out/bin/shaula capabilities list --json 2>&1 || true)"
if printf '%s\n' "${capabilities_json}" | jq -e '.ok == true and (.capture | has("area") and has("fullscreen") and has("window"))' >/dev/null 2>&1; then
  append_check "command_family.capabilities" true "pass"
else
  append_check "command_family.capabilities" false "invalid_contract output=$(printf '%s' "${capabilities_json}" | tr '\n' ' ')"
fi

history_json="$(./zig-out/bin/shaula history list --json 2>&1 || true)"
if printf '%s\n' "${history_json}" | jq -e '.ok == true and (.result.entries | type == "array")' >/dev/null 2>&1; then
  append_check "command_family.history" true "pass"
else
  append_check "command_family.history" false "invalid_contract output=$(printf '%s' "${history_json}" | tr '\n' ' ')"
fi

capture_json="$(./zig-out/bin/shaula capture area --json --dry-run 2>&1 || true)"
if printf '%s\n' "${capture_json}" | jq -e '.ok == true and .command == "capture area" and .selection.cancelled == false' >/dev/null 2>&1; then
  append_check "command_family.capture" true "pass"
else
  append_check "command_family.capture" false "invalid_contract output=$(printf '%s' "${capture_json}" | tr '\n' ' ')"
fi

set +e
daemon_status_json="$(./zig-out/bin/shaula daemon status --json 2>&1)"
daemon_status_rc=$?
set -e
if [[ ${daemon_status_rc} -eq 0 ]]; then
  if printf '%s\n' "${daemon_status_json}" | jq -e '(.state|type=="string") and (.result.state|type=="string")' >/dev/null 2>&1; then
    append_check "command_family.daemon" true "pass"
  else
    append_check "command_family.daemon" false "invalid_success_contract output=$(printf '%s' "${daemon_status_json}" | tr '\n' ' ')"
  fi
else
  if printf '%s\n' "${daemon_status_json}" | jq -e '.ok == false and (.error.code == "ERR_DAEMON_NOT_RUNNING" or .error.code == "ERR_IPC_TIMEOUT")' >/dev/null 2>&1; then
    append_check "command_family.daemon" true "pass_expected_failure"
  else
    append_check "command_family.daemon" false "invalid_failure_contract output=$(printf '%s' "${daemon_status_json}" | tr '\n' ' ')"
  fi
fi

blocking_issues="$(jq -r 'length' <<<"${blockers_json}")"
ready=true
if [[ "${blocking_issues}" -gt 0 ]]; then
  ready=false
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson ready "${ready}" \
  --argjson blocking_issues "${blocking_issues}" \
  --argjson blockers "${blockers_json}" \
  --argjson checks "${checks_json}" \
  '{
    suite: "task-15-release-readiness",
    timestamp: $timestamp,
    ready: $ready,
    blocking_issues: $blocking_issues,
    error_code: (if $ready then null else "ERR_RELEASE_BLOCKED" end),
    blockers: $blockers,
    checks: $checks
  }' > "${REPORT_JSON}"

if [[ "${ready}" != "true" ]]; then
  {
    echo "ERR_RELEASE_BLOCKED blocking_issues=${blocking_issues}"
    jq -r '.[] | "- \(.)"' <<<"${blockers_json}"
    echo "report=${REPORT_JSON}"
  } > "${ERROR_TXT}"

  echo "ERR_RELEASE_BLOCKED blocking_issues=${blocking_issues} report=${REPORT_JSON} error_log=${ERROR_TXT}" >&2
  exit 1
fi

echo "ok release_readiness_capture_fix report=${REPORT_JSON}"
