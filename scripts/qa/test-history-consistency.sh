#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

CAPTURE_PATH="/tmp/shaula/task10-history-consistency.png"
rm -f "${CAPTURE_PATH}"

capture_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture fullscreen --json --save --output "${CAPTURE_PATH}")"

printf '%s\n' "${capture_json}" | jq -e '
  .ok == true and
  .saved.ok == true and
  (.saved.path | length > 0)
' >/dev/null || {
  echo "ERR_HISTORY_CONSISTENCY_INVALID reason=capture_saved_contract" >&2
  exit 1
}

history_json="$(./zig-out/bin/shaula history list --json)"
printf '%s\n' "${history_json}" | jq -e '
  .ok == true and
  .command == "history list" and
  (.result.entries | type == "array") and
  (.result.entries | length >= 1) and
  (.result.entries[0].path | length > 0)
' >/dev/null || {
  echo "ERR_HISTORY_CONSISTENCY_INVALID reason=history_schema" >&2
  exit 1
}

capture_path="$(printf '%s\n' "${capture_json}" | jq -r '.saved.path')"
history_path="$(printf '%s\n' "${history_json}" | jq -r '.result.entries[0].path')"

[[ "${capture_path}" == "${history_path}" ]] || {
  echo "ERR_HISTORY_CONSISTENCY_INVALID reason=latest_path_mismatch" >&2
  exit 1
}

echo "ok history_consistency deterministic"
