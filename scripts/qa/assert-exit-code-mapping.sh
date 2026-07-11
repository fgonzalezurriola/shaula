#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

./dev build >/dev/null

assert_exit() {
  local expected="$1"
  shift

  set +e
  "$@" >/tmp/shaula-qa-exit.out 2>/tmp/shaula-qa-exit.err
  local rc=$?
  set -e

  if [[ "${rc}" -ne "${expected}" ]]; then
    echo "ERR_EXIT_CODE_MAPPING_MISMATCH expected=${expected} got=${rc} command=$*" >&2
    cat /tmp/shaula-qa-exit.out >&2 || true
    cat /tmp/shaula-qa-exit.err >&2 || true
    exit 1
  fi
}

assert_exit 2 ./build/shaula
assert_exit 10 env SHAULA_COMPOSITOR=x11 ./build/shaula preflight --json
assert_exit 11 env -u WAYLAND_DISPLAY SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock ./build/shaula preflight --json
assert_exit 34 env SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture window --json
assert_exit 51 env SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture fullscreen --json --output /tmp/::invalid::/path.png
assert_exit 37 env SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=timeout SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture area --json --dry-run
assert_exit 38 env SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=malformed SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture area --json --dry-run
assert_exit 99 env SHAULA_INJECT_UNKNOWN_FAILURE=1 SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture fullscreen --json --no-preview

errors_json="$(./build/shaula errors list --json)"
printf '%s\n' "${errors_json}" | jq -e '
  .ok == true and
  .command == "errors list" and
  (.result.errors | length > 0) and
  (all(.result.errors[]; (.code | startswith("ERR_")) and (.exit_code | type == "number")))
' >/dev/null || {
  echo "ERR_EXIT_CODE_MAPPING_MISMATCH reason=errors_list_contract" >&2
  exit 1
}

echo "ok exit_code_mapping deterministic"
