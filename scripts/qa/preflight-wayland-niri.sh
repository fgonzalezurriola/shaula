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

zig build >/dev/null

QA_SOCKET_PATH="$(mktemp -u "/tmp/shaula-preflight-ipc.XXXXXX.sock")"
DAEMON_LOG_PATH="$(mktemp "/tmp/shaula-preflight-daemon.XXXXXX.log")"
DAEMON_PID=""

cleanup() {
  set +e
  if [[ -n "${DAEMON_PID}" ]]; then
    kill "${DAEMON_PID}" >/dev/null 2>&1 || true
    wait "${DAEMON_PID}" >/dev/null 2>&1 || true
  fi
  rm -f "${QA_SOCKET_PATH}" >/dev/null 2>&1 || true
  rm -f "${DAEMON_LOG_PATH}" >/dev/null 2>&1 || true
  set -e
}
trap cleanup EXIT

./zig-out/bin/shaula daemon _serve --json --socket "${QA_SOCKET_PATH}" >"${DAEMON_LOG_PATH}" 2>&1 &
DAEMON_PID=$!

daemon_ready=0
for _ in $(seq 1 40); do
  if [[ -S "${QA_SOCKET_PATH}" ]]; then
    daemon_ready=1
    break
  fi
  if ! kill -0 "${DAEMON_PID}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.05
done

if [[ ${daemon_ready} -ne 1 ]]; then
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=daemon_start_failed" >&2
  if [[ -s "${DAEMON_LOG_PATH}" ]]; then
    cat "${DAEMON_LOG_PATH}" >&2
  fi
  exit 1
fi

set +e
preflight_json="$(SHAULA_COMPOSITOR=niri NIRI_SOCKET="${NIRI_SOCKET}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY}" SHAULA_SOCKET="${QA_SOCKET_PATH}" ./zig-out/bin/shaula preflight --json 2>&1)"
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
  .result.ipc_ready == true
' >/dev/null || {
  echo "ERR_PREFLIGHT_ENV_NOT_READY reason=preflight_json_not_ready" >&2
  printf '%s\n' "${preflight_json}" >&2
  exit 1
}

echo "ok qa_preflight_wayland_niri"
