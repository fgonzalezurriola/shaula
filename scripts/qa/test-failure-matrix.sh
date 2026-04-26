#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

assert_failure_code() {
  local expected_code="$1"
  shift

  set +e
  local json
  json="$($@)"
  local rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    echo "ERR_FAILURE_MATRIX_INVALID reason=expected_nonzero code=${expected_code} command=$*" >&2
    exit 1
  fi

  printf '%s\n' "${json}" | jq -e --arg code "${expected_code}" '
    .ok == false and
    .error.code == $code and
    (.error.retryable | type == "boolean") and
    (.error.details | type == "object")
  ' >/dev/null || {
    echo "ERR_FAILURE_MATRIX_INVALID reason=error_envelope_mismatch expected=${expected_code}" >&2
    printf '%s\n' "${json}" >&2
    exit 1
  }
}

assert_failure_code ERR_UNSUPPORTED_COMPOSITOR env SHAULA_COMPOSITOR=sway ./zig-out/bin/shaula preflight --json
assert_failure_code ERR_PREFLIGHT_ENV_NOT_READY env -u WAYLAND_DISPLAY SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock ./zig-out/bin/shaula preflight --json
assert_failure_code ERR_IPC_BIND_FAILED env ./zig-out/bin/shaula daemon _serve --json --socket /dev/null/daemon.sock
assert_failure_code ERR_CAPTURE_MODE_UNSUPPORTED env SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture window --json
assert_failure_code ERR_CLIPBOARD_UNAVAILABLE env SHAULA_CLIPBOARD_AVAILABLE=0 ./zig-out/bin/shaula clipboard import-image --json
assert_failure_code ERR_OUTPUT_PATH_INVALID env SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output /tmp/::invalid::/path.png
assert_failure_code ERR_OVERLAY_TIMEOUT env SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=timeout SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --dry-run
assert_failure_code ERR_OVERLAY_PROTOCOL_INVALID env SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=malformed SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --dry-run
assert_failure_code ERR_UNKNOWN_UNMAPPED env SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1 SHAULA_INJECT_UNKNOWN_FAILURE=1 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json

echo "ok failure_matrix deterministic"
