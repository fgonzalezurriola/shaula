#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

schema_expr='
  has("ok") and
  has("contract_version") and
  has("command") and
  has("timestamp") and
  has("mode") and
  has("path") and
  has("mime") and
  has("dimensions") and
  (.dimensions | has("width") and has("height")) and
  has("backend_used") and
  has("latency_ms") and
  has("warnings")
'

area_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --no-preview)"
printf '%s\n' "${area_json}" | jq -e "${schema_expr}" >/dev/null || {
  echo "ERR_CAPTURE_SCHEMA_INVALID reason=area_shape" >&2
  exit 1
}

fullscreen_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture fullscreen --json)"
printf '%s\n' "${fullscreen_json}" | jq -e "${schema_expr}" >/dev/null || {
  echo "ERR_CAPTURE_SCHEMA_INVALID reason=fullscreen_shape" >&2
  exit 1
}

set +e
window_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture window --json 2>/dev/null)"
window_rc=$?
set -e

printf '%s\n' "${window_json}" | jq -e '
  .mode == "window" and
  .ok == false and
  .degraded == true and
  .error.code == "ERR_CAPTURE_MODE_UNSUPPORTED" and
  has("backend_used")
' >/dev/null || {
  echo "ERR_CAPTURE_SCHEMA_INVALID reason=window_degradation_shape" >&2
  exit 1
}

if [[ ${window_rc} -eq 0 ]]; then
  echo "ERR_CAPTURE_SCHEMA_INVALID reason=window_should_fail" >&2
  exit 1
fi

echo "ok capture_result_schema contract=1.0.0"
