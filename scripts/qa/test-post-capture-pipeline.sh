#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if ! command -v sha256sum >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=sha256sum" >&2
  exit 1
fi

zig build >/dev/null

CAPTURE_PATH="/tmp/shaula/task10-post-pipeline.png"
rm -f "${CAPTURE_PATH}"

json_ok="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --no-preview --save --copy --output "${CAPTURE_PATH}")"

printf '%s\n' "${json_ok}" | jq -e '
  .ok == true and
  .saved.ok == true and
  .clipboard.ok == true and
  (.saved.path | length > 0)
' >/dev/null || {
  echo "ERR_POST_CAPTURE_PIPELINE_INVALID reason=save_copy_contract" >&2
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
  echo "ERR_POST_CAPTURE_PIPELINE_INVALID reason=history_missing_entry" >&2
  exit 1
}

captured_path="$(printf '%s\n' "${json_ok}" | jq -r '.saved.path')"
history_path="$(printf '%s\n' "${history_json}" | jq -r '.result.entries[0].path')"

[[ "${captured_path}" == "${history_path}" ]] || {
  echo "ERR_POST_CAPTURE_PIPELINE_INVALID reason=path_mismatch" >&2
  exit 1
}

file_hash="$(sha256sum "${captured_path}" | awk '{print $1}')"
history_hash="$(sha256sum "${history_path}" | awk '{print $1}')"

[[ "${file_hash}" == "${history_hash}" ]] || {
  echo "ERR_POST_CAPTURE_PIPELINE_INVALID reason=hash_mismatch" >&2
  exit 1
}

json_partial="$(SHAULA_CLIPBOARD_AVAILABLE=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --no-preview --save --copy --output "${CAPTURE_PATH}")"

printf '%s\n' "${json_partial}" | jq -e '
  .ok == true and
  .saved.ok == true and
  .clipboard.ok == false and
  .clipboard.error.code == "ERR_CLIPBOARD_UNAVAILABLE" and
  .partial == true
' >/dev/null || {
  echo "ERR_POST_CAPTURE_PIPELINE_INVALID reason=partial_success_contract" >&2
  exit 1
}

echo "ok post_capture_pipeline deterministic"
