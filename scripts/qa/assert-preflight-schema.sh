#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

preflight_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula preflight --json)"
printf '%s\n' "${preflight_json}" | jq -e '
  .ok == true and
  .command == "preflight" and
  .compositor == "niri" and
  (.ipc | has("socket") and has("ready")) and
  (.result | has("wayland") and has("ipc_ready"))
' >/dev/null || {
  echo "ERR_PREFLIGHT_SCHEMA_INVALID reason=preflight_shape" >&2
  exit 1
}

capabilities_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock ./zig-out/bin/shaula capabilities list --json)"
printf '%s\n' "${capabilities_json}" | jq -e '
  .ok == true and
  .command == "capabilities list" and
  (.capture.area == true) and
  (.capture.fullscreen == true) and
  (.capture.all_screens == true) and
  (.capture | has("window")) and
  has("backend") and
  has("fallbacks") and
  (.backend == .result.backend)
' >/dev/null || {
  echo "ERR_CAPABILITIES_SCHEMA_INVALID reason=capabilities_shape" >&2
  exit 1
}

unsupported_json="$(SHAULA_COMPOSITOR=sway ./zig-out/bin/shaula preflight --json || true)"
printf '%s\n' "${unsupported_json}" | jq -e '.ok == false and .error.code == "ERR_UNSUPPORTED_COMPOSITOR"' >/dev/null || {
  echo "ERR_PREFLIGHT_SCHEMA_INVALID reason=unsupported_token" >&2
  exit 1
}

echo "ok preflight_schema contract=1.0.0"
