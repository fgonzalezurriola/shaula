#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

./dev build >/dev/null

capabilities_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock ./build/shaula capabilities list --json)"

printf '%s\n' "${capabilities_json}" | jq -e '
  .ok == true and
  .command == "capabilities list" and
  (.backend | type == "string" and length > 0) and
  (.result.backend == .backend) and
  (.result.capture == .capture) and
  (.capture | has("area") and has("fullscreen") and has("all_screens") and has("window"))
' >/dev/null || {
  echo "ERR_CAPABILITY_EXECUTION_MISMATCH reason=capabilities_contract_shape" >&2
  exit 1
}

backend_label="$(printf '%s\n' "${capabilities_json}" | jq -r '.backend')"

capture_and_assert_mode() {
  local mode="$1"
  local supported="$2"
  shift 2

  local capture_json=""
  local capture_rc=0

  set +e
  capture_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture "${mode}" --json "$@" 2>&1)"
  capture_rc=$?
  set -e

  if [[ "${supported}" == "true" ]]; then
    if [[ ${capture_rc} -ne 0 ]]; then
      echo "ERR_CAPABILITY_EXECUTION_MISMATCH reason=supported_mode_failed mode=${mode}" >&2
      printf '%s\n' "${capture_json}" >&2
      exit 1
    fi

    printf '%s\n' "${capture_json}" | jq -e --arg mode "${mode}" --arg backend "${backend_label}" '
      .ok == true and
      .mode == $mode and
      .backend_used == $backend and
      (.path | type == "string" and length > 0)
    ' >/dev/null || {
      echo "ERR_CAPABILITY_EXECUTION_MISMATCH reason=supported_mode_contract mode=${mode}" >&2
      printf '%s\n' "${capture_json}" >&2
      exit 1
    }
    return
  fi

  if [[ ${capture_rc} -eq 0 ]]; then
    echo "ERR_CAPABILITY_EXECUTION_MISMATCH reason=unsupported_mode_succeeded mode=${mode}" >&2
    printf '%s\n' "${capture_json}" >&2
    exit 1
  fi

  printf '%s\n' "${capture_json}" | jq -e --arg mode "${mode}" --arg backend "${backend_label}" '
    .ok == false and
    .mode == $mode and
    .backend_used == $backend and
    .error.code == "ERR_CAPTURE_MODE_UNSUPPORTED"
  ' >/dev/null || {
    echo "ERR_CAPABILITY_EXECUTION_MISMATCH reason=unsupported_mode_contract mode=${mode}" >&2
    printf '%s\n' "${capture_json}" >&2
    exit 1
  }
}

area_supported="$(printf '%s\n' "${capabilities_json}" | jq -r '.capture.area')"
fullscreen_supported="$(printf '%s\n' "${capabilities_json}" | jq -r '.capture.fullscreen')"
all_screens_supported="$(printf '%s\n' "${capabilities_json}" | jq -r '.capture.all_screens')"
window_supported="$(printf '%s\n' "${capabilities_json}" | jq -r '.capture.window')"

capture_and_assert_mode area "${area_supported}" --no-preview --output /tmp/shaula/task2-capability-area.png
capture_and_assert_mode fullscreen "${fullscreen_supported}" --no-preview --output /tmp/shaula/task2-capability-fullscreen.png
capture_and_assert_mode all-screens "${all_screens_supported}" --no-preview --output /tmp/shaula/task2-capability-all-screens.png
capture_and_assert_mode window "${window_supported}" --window-id 26 --output /tmp/shaula/task2-capability-window.png

echo "ok capability_execution_strict_parity"
