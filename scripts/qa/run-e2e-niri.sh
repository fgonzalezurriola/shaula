#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-11-layer-e2e-niri-report.json"
ERROR_LOG="${EVIDENCE_DIR}/task-11-layer-e2e-niri-report-error.txt"

mkdir -p "${EVIDENCE_DIR}"
: > "${ERROR_LOG}"

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
# Deterministic defaults for headless/CI where these may be unset.
NIRI_SOCKET="${NIRI_SOCKET:-/tmp/niri.sock}"
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export NIRI_SOCKET
export WAYLAND_DISPLAY
RUN_TS="$(date -u +"%Y%m%dT%H%M%SZ")"
RUN_DIR="/tmp/shaula/runs/${RUN_TS}-e2e-${QA_PROFILE}"
mkdir -p "${RUN_DIR}"
ln -sfn "${RUN_DIR}" /tmp/shaula/runs/latest

cleanup_qa_artifacts() {
  if [[ "${KEEP_ARTIFACTS}" == "1" ]]; then
    return
  fi

  rm -f /tmp/shaula/task17-e2e-*.png \
        /tmp/shaula/task8-*.png \
        /tmp/shaula/task8-*.token \
        /tmp/shaula/task9-*.png \
        /tmp/shaula/task2-capability-*.png \
        /tmp/shaula/task3-capture-content-fullscreen.png \
        /tmp/shaula/task3-stub-signature-1x1.png \
        /tmp/shaula/qa-runtime-capture.png 2>/dev/null || true
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
  local status="fail"

  set +e
  output="$(bash -lc "${cmd}" 2>&1)"
  rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    status="pass"
    pass=true
  elif [[ "${id}" == "e2e.preflight.wayland_niri" ]] && grep -Eq 'ERR_PREFLIGHT_ENV_NOT_READY reason=(missing_wayland_display|missing_niri_socket|niri_socket_not_unix_socket)' <<<"${output}"; then
    # In headless/CI we allow missing real Niri socket as degraded preflight only.
    status="degraded"
    pass=true
  else
    status="fail"
    failed_checks="$(jq -c -n       --argjson current "${failed_checks}"       --arg id "${id}"       --arg cmd "${cmd}"       '$current + [{ id: $id, command: $cmd }]'     )"
  fi

  printf '[%s] rc=%s status=%s cmd=%s\n%s\n\n' "${id}" "${rc}" "${status}" "${cmd}" "${output}" >> "${ERROR_LOG}"

  subchecks_json="$(jq -c -n     --argjson current "${subchecks_json}"     --arg id "${id}"     --arg cmd "${cmd}"     --arg output "${output}"     --arg status "${status}"     --argjson pass "${pass}"     '$current + [{ id: $id, status: $status, pass: $pass, command: $cmd, output: $output }]'   )"
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

run_subcheck "e2e.preflight.wayland_niri" "bash ./scripts/qa/preflight-wayland-niri.sh"
run_subcheck "e2e.build.zig" "zig build >/dev/null"

AREA_PATH="/tmp/shaula/task17-e2e-area.png"
FULL_PATH="/tmp/shaula/task17-e2e-fullscreen.png"
WIN_PATH="/tmp/shaula/task17-e2e-window.png"
rm -f "${AREA_PATH}" "${FULL_PATH}" "${WIN_PATH}"

run_subcheck "e2e.capture.area" "AREA_PATH=/tmp/shaula/task17-e2e-area.png; rm -f \"\${AREA_PATH}\"; AREA_JSON=\$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY}\" ./zig-out/bin/shaula capture area --json --output \"\${AREA_PATH}\"); printf '%s\\n' \"\${AREA_JSON}\" | jq -e '.ok==true and .mode==\"area\" and (.path|length>0)' >/dev/null"

run_subcheck "e2e.capture.fullscreen" "FULL_PATH=/tmp/shaula/task17-e2e-fullscreen.png; rm -f \"\${FULL_PATH}\"; FULL_JSON=\$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY}\" ./zig-out/bin/shaula capture fullscreen --json --output \"\${FULL_PATH}\"); printf '%s\\n' \"\${FULL_JSON}\" | jq -e '.ok==true and .mode==\"fullscreen\" and (.path|length>0)' >/dev/null"

run_subcheck "e2e.capture.window" "WIN_PATH=/tmp/shaula/task17-e2e-window.png; rm -f \"\${WIN_PATH}\"; set +e; WIN_JSON=\$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY}\" ./zig-out/bin/shaula capture window --json --output \"\${WIN_PATH}\" 2>&1); WIN_RC=\$?; set -e; if [[ \${WIN_RC} -eq 0 ]]; then printf '%s\\n' \"\${WIN_JSON}\" | jq -e '.ok==true and .mode==\"window\"' >/dev/null; else printf '%s\\n' \"\${WIN_JSON}\" | jq -e '.ok==false and .error.code==\"ERR_CAPTURE_MODE_UNSUPPORTED\"' >/dev/null; fi"

run_subcheck "e2e.clipboard.path" "AREA_PATH=/tmp/shaula/task17-e2e-area.png; CLIP_JSON=\$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY}\" ./zig-out/bin/shaula capture area --json --save --copy --output \"\${AREA_PATH}\"); printf '%s\\n' \"\${CLIP_JSON}\" | jq -e '.ok==true and .saved.ok==true and (.clipboard.ok|type==\"boolean\")' >/dev/null"

run_subcheck "e2e.failure.unsupported_compositor" "set +e; UNSUPPORTED_JSON=\$(SHAULA_COMPOSITOR=sway ./zig-out/bin/shaula preflight --json 2>&1); UNSUPPORTED_RC=\$?; set -e; [[ \${UNSUPPORTED_RC} -ne 0 ]] && printf '%s\\n' \"\${UNSUPPORTED_JSON}\" | jq -e '.ok==false and .error.code==\"ERR_UNSUPPORTED_COMPOSITOR\"' >/dev/null"

run_subcheck "e2e.failure.permission_clipboard" "AREA_PATH=/tmp/shaula/task17-e2e-area.png; DEGRADED_JSON=\$(SHAULA_CLIPBOARD_AVAILABLE=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY}\" ./zig-out/bin/shaula capture area --json --save --copy --output \"\${AREA_PATH}\"); printf '%s\\n' \"\${DEGRADED_JSON}\" | jq -e '.ok==true and .saved.ok==true and .clipboard.ok==false and .clipboard.error.code==\"ERR_CLIPBOARD_UNAVAILABLE\" and .partial==true' >/dev/null"

run_subcheck "e2e.backend.daemon_states" "SOCKET=/tmp/shaula-task17-e2e.sock; START_JSON=\$(SHAULA_SOCKET=\"\${SOCKET}\" ./zig-out/bin/shaula daemon start --json); printf '%s\\n' \"\${START_JSON}\" | jq -e '.status==\"ready\" and .result.state==\"ready\"' >/dev/null && STATUS_JSON=\$(SHAULA_SOCKET=\"\${SOCKET}\" ./zig-out/bin/shaula daemon status --json); printf '%s\\n' \"\${STATUS_JSON}\" | jq -e '.state|IN(\"ready\",\"degraded\")' >/dev/null && STOP_JSON=\$(SHAULA_SOCKET=\"\${SOCKET}\" ./zig-out/bin/shaula daemon stop --json); printf '%s\\n' \"\${STOP_JSON}\" | jq -e '.stopped==true' >/dev/null"

run_subcheck "e2e.capture.capabilities.strict_contract" "CAPS_JSON=\$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=\"${NIRI_SOCKET}\" ./zig-out/bin/shaula capabilities list --json); printf '%s\\n' \"\${CAPS_JSON}\" | jq -e '.ok==true and has(\"backend\") and has(\"fallbacks\") and (.capture|has(\"window\"))' >/dev/null && bash ./scripts/qa/assert-capabilities-consistency.sh"
if [[ "${QA_PROFILE}" == "full" || "${QA_PROFILE}" == "debug" ]]; then
  if [[ "${ALLOW_INTRUSIVE_UI}" == "1" ]]; then
    run_subcheck "e2e.capture.shell_artifact_guard.panel_hide" "bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh"
  else
    record_degraded_subcheck "e2e.capture.shell_artifact_guard.panel_hide" "bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh" "ERR_QA_INTRUSIVE_UI_DISABLED_BY_POLICY" "interactive_opt_in_required"
  fi
  run_subcheck "e2e.noctalia.optional_integration" "bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin"
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
suite_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${subchecks_json}" >/dev/null && echo true || echo false)"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson pass "${suite_pass}" \
  --arg ui_policy_mode "${UI_POLICY_MODE}" \
  --argjson subchecks "${subchecks_json}" \
  '{
    suite: "task-11-layer-e2e-niri",
    timestamp: $timestamp,
    pass: $pass,
    ui_policy_mode: $ui_policy_mode,
    script: "scripts/qa/run-e2e-niri.sh",
    subchecks: $subchecks
  }' > "${REPORT_JSON}"

if [[ "${suite_pass}" != "true" ]]; then
  echo "QA failure summary (e2e niri):" >&2
  jq -r '.[] | "- \(.id): \(.command)"' <<<"${failed_checks}" >&2
  echo "ERR_QA_E2E_NIRI_FAILED report=${REPORT_JSON} log=${ERROR_LOG}" >&2
  exit 1
fi

echo "ok qa_e2e_niri profile=${QA_PROFILE} keep_artifacts=${KEEP_ARTIFACTS} run_dir=${RUN_DIR} mode=${UI_POLICY_MODE}"
