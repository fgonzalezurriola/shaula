#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

AREA_PATH="/tmp/shaula/task9-area.png"
FULL_PATH="/tmp/shaula/task9-fullscreen.png"

rm -f "${AREA_PATH}" "${FULL_PATH}"

area_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output "${AREA_PATH}")"
printf '%s\n' "${area_json}" | jq -e '
  .ok == true and
  .mode == "area" and
  .mime == "image/png" and
  (.path | length > 0) and
  (.dimensions.width > 0 and .dimensions.height > 0) and
  (.backend_used | length > 0) and
  (.latency_ms >= 0)
' >/dev/null || {
  echo "ERR_CAPTURE_CORE_INVALID reason=area_contract" >&2
  exit 1
}

[[ -f "${AREA_PATH}" ]] || {
  echo "ERR_CAPTURE_CORE_INVALID reason=area_file_missing" >&2
  exit 1
}

fullscreen_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture fullscreen --json --output "${FULL_PATH}")"
printf '%s\n' "${fullscreen_json}" | jq -e '
  .ok == true and
  .mode == "fullscreen" and
  .mime == "image/png" and
  (.path | length > 0) and
  (.dimensions.width > 0 and .dimensions.height > 0) and
  (.backend_used | length > 0) and
  (.latency_ms >= 0)
' >/dev/null || {
  echo "ERR_CAPTURE_CORE_INVALID reason=fullscreen_contract" >&2
  exit 1
}

[[ -f "${FULL_PATH}" ]] || {
  echo "ERR_CAPTURE_CORE_INVALID reason=fullscreen_file_missing" >&2
  exit 1
}

echo "ok capture_core_modes area+fullscreen"
