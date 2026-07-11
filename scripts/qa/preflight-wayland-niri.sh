#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=missing_wayland_display" >&2
  exit 1
fi

if [[ -z "${NIRI_SOCKET:-}" ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=missing_niri_socket" >&2
  exit 1
fi

if [[ ! -S "${NIRI_SOCKET}" ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=niri_socket_not_unix_socket socket=${NIRI_SOCKET}" >&2
  exit 1
fi

./dev build >/dev/null

set +e
preflight_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" ./build/shaula preflight --json 2>&1)"
preflight_rc=$?
set -e

if [[ ${preflight_rc} -ne 0 ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=preflight_command_failed" >&2
  printf '%s\n' "${preflight_json}" >&2
  exit 1
fi

printf '%s\n' "${preflight_json}" | jq -e '
  .ok == true and
  .compositor == "niri" and
  .result.wayland == true and
  (.result.backend | type == "string") and
  (.result.portal_available | type == "boolean")
' >/dev/null || {
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=preflight_json_not_ready" >&2
  printf '%s\n' "${preflight_json}" >&2
  exit 1
}

echo "ok qa_preflight_wayland_niri"
