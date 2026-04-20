#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-10-layer-e2e-niri-report.json"

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

bash ./scripts/qa/preflight-wayland-niri.sh

zig build >/dev/null

AREA_PATH="/tmp/shaula/task17-e2e-area.png"
FULL_PATH="/tmp/shaula/task17-e2e-fullscreen.png"
WIN_PATH="/tmp/shaula/task17-e2e-window.png"
rm -f "${AREA_PATH}" "${FULL_PATH}" "${WIN_PATH}"

area_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./zig-out/bin/shaula capture area --json --output "${AREA_PATH}")"
printf '%s\n' "${area_json}" | jq -e '.ok==true and .mode=="area" and (.path|length>0)' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=area_capture_contract" >&2
  exit 1
}

fullscreen_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./zig-out/bin/shaula capture fullscreen --json --output "${FULL_PATH}")"
printf '%s\n' "${fullscreen_json}" | jq -e '.ok==true and .mode=="fullscreen" and (.path|length>0)' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=fullscreen_capture_contract" >&2
  exit 1
}

set +e
window_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./zig-out/bin/shaula capture window --json --output "${WIN_PATH}" 2>&1)"
window_rc=$?
set -e
if [[ ${window_rc} -eq 0 ]]; then
  printf '%s\n' "${window_json}" | jq -e '.ok==true and .mode=="window"' >/dev/null || {
    echo "ERR_E2E_NIRI_INVALID reason=window_success_shape" >&2
    exit 1
  }
else
  printf '%s\n' "${window_json}" | jq -e '.ok==false and .error.code=="ERR_CAPTURE_MODE_UNSUPPORTED"' >/dev/null || {
    echo "ERR_E2E_NIRI_INVALID reason=window_failure_shape" >&2
    exit 1
  }
fi

clipboard_ok_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./zig-out/bin/shaula capture area --json --save --copy --output "${AREA_PATH}")"
printf '%s\n' "${clipboard_ok_json}" | jq -e '.ok==true and .saved.ok==true and (.clipboard.ok|type=="boolean")' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=clipboard_path_contract" >&2
  exit 1
}

set +e
unsupported_json="$(SHAULA_COMPOSITOR=sway ./zig-out/bin/shaula preflight --json 2>&1)"
unsupported_rc=$?
set -e
if [[ ${unsupported_rc} -eq 0 ]]; then
  echo "ERR_E2E_NIRI_INVALID reason=unsupported_preflight_expected_failure" >&2
  exit 1
fi
printf '%s\n' "${unsupported_json}" | jq -e '.ok==false and .error.code=="ERR_UNSUPPORTED_COMPOSITOR"' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=unsupported_preflight_error_code" >&2
  exit 1
}

clipboard_degraded_json="$(SHAULA_CLIPBOARD_AVAILABLE=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./zig-out/bin/shaula capture area --json --save --copy --output "${AREA_PATH}")"
printf '%s\n' "${clipboard_degraded_json}" | jq -e '
  .ok==true and
  .saved.ok==true and
  .clipboard.ok==false and
  .clipboard.error.code=="ERR_CLIPBOARD_UNAVAILABLE" and
  .partial==true
' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=clipboard_degraded_path" >&2
  exit 1
}

SOCKET="/tmp/shaula-task17-e2e.sock"
start_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json)"
printf '%s\n' "${start_json}" | jq -e '.status=="ready" and .result.state=="ready"' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=daemon_start_state" >&2
  exit 1
}

status_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon status --json)"
printf '%s\n' "${status_json}" | jq -e '.state|IN("ready","degraded")' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=daemon_status_state" >&2
  exit 1
}

stop_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json)"
printf '%s\n' "${stop_json}" | jq -e '.stopped==true' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=daemon_stop_state" >&2
  exit 1
}

capabilities_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" ./zig-out/bin/shaula capabilities list --json)"
printf '%s\n' "${capabilities_json}" | jq -e '.ok==true and has("backend") and has("fallbacks") and (.capture|has("window"))' >/dev/null || {
  echo "ERR_E2E_NIRI_INVALID reason=capabilities_backend_state" >&2
  exit 1
}

bash ./scripts/qa/assert-capabilities-consistency.sh
bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh
bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  '{
    suite: "task-10-layer-e2e-niri",
    timestamp: $timestamp,
    pass: true,
    script: "scripts/qa/run-e2e-niri.sh",
    subchecks: [
      { id: "e2e.preflight.wayland_niri", pass: true },
      { id: "e2e.capture.area", pass: true },
      { id: "e2e.capture.fullscreen", pass: true },
      { id: "e2e.capture.window", pass: true },
      { id: "e2e.clipboard.path", pass: true },
      { id: "e2e.failure.unsupported_compositor", pass: true },
      { id: "e2e.failure.permission_clipboard", pass: true },
      { id: "e2e.backend.daemon_states", pass: true },
      { id: "e2e.capture.capabilities.strict_contract", pass: true },
      { id: "e2e.capture.shell_artifact_guard.panel_hide", pass: true },
      { id: "e2e.noctalia.optional_integration", pass: true }
    ]
  }' > "${REPORT_JSON}"

echo "ok qa_e2e_niri"
