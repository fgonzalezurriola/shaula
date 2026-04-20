#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
EVIDENCE_OK="${EVIDENCE_DIR}/task-4-daemon-status-truth.txt"
EVIDENCE_ERR="${EVIDENCE_DIR}/task-4-daemon-status-truth-error.txt"
mkdir -p "${EVIDENCE_DIR}"

zig build >/dev/null

SOCKET="${SHAULA_SOCKET:-/tmp/shaula-task4-status-truth.sock}"
ORPHAN_SOCKET="${SOCKET}.orphan"
ORPHAN_PID=""

cleanup() {
  set +e
  if [[ -n "${ORPHAN_PID}" ]]; then
    kill "${ORPHAN_PID}" >/dev/null 2>&1 || true
    wait "${ORPHAN_PID}" >/dev/null 2>&1 || true
  fi
  SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json >/dev/null 2>&1 || true
  rm -f "${SOCKET}" "${ORPHAN_SOCKET}"
  set -e
}
trap cleanup EXIT

rm -f "${EVIDENCE_OK}" "${EVIDENCE_ERR}"
SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json >/dev/null 2>&1 || true
rm -f "${SOCKET}" "${ORPHAN_SOCKET}"

ipc_request_json() {
  local socket_path="$1"
  local command="$2"
  local request_id="$3"
  python3 - "${socket_path}" "${command}" "${request_id}" <<'PY'
import json
import socket
import sys

socket_path = sys.argv[1]
command = sys.argv[2]
request_id = sys.argv[3]

payload = {
    "ipc_version": "1.0.0",
    "request_id": request_id,
    "command": command,
}

client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.settimeout(1.5)
client.connect(socket_path)
client.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))

buffer = b""
while not buffer.endswith(b"\n"):
    chunk = client.recv(4096)
    if not chunk:
        break
    buffer += chunk
client.close()

if not buffer:
    raise SystemExit("ERR_DAEMON_STATUS_TRUTH_EMPTY_IPC_RESPONSE")

print(buffer.decode("utf-8").strip())
PY
}

start_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json)"
printf '%s\n' "${start_json}" | jq -e '.ok==true and .result.state=="ready"' >/dev/null

ipc_ready_json="$(ipc_request_json "${SOCKET}" "daemon.status" "task4-ready")"
printf '%s\n' "${ipc_ready_json}" | jq -e '.ok==true and ((.daemon_state // .result.state) == "ready") and (.ipc_version == "1.0.0")' >/dev/null

cli_ready_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon status --json)"
printf '%s\n' "${cli_ready_json}" | jq -e '
  .ok==true and
  (.state == .result.state) and
  (.result.state|IN("ready","capturing","degraded")) and
  (.result.ipc_version == "1.0.0")
' >/dev/null

ready_ipc_state="$(printf '%s\n' "${ipc_ready_json}" | jq -r '.daemon_state // .result.state')"
ready_cli_state="$(printf '%s\n' "${cli_ready_json}" | jq -r '.result.state')"
if [[ "${ready_ipc_state}" != "${ready_cli_state}" ]]; then
  echo "ERR_DAEMON_STATUS_TRUTH_MISMATCH phase=ready ipc=${ready_ipc_state} cli=${ready_cli_state}" >&2
  exit 1
fi

capture_begin_json="$(ipc_request_json "${SOCKET}" "daemon.capture.begin" "task4-capture-begin")"
printf '%s\n' "${capture_begin_json}" | jq -e '.ok==true and ((.daemon_state // .result.state) == "capturing")' >/dev/null

ipc_capturing_json="$(ipc_request_json "${SOCKET}" "daemon.status" "task4-capturing")"
printf '%s\n' "${ipc_capturing_json}" | jq -e '.ok==true and ((.daemon_state // .result.state) == "capturing") and (.ipc_version == "1.0.0")' >/dev/null

cli_capturing_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon status --json)"
printf '%s\n' "${cli_capturing_json}" | jq -e '
  .ok==true and
  (.state == "capturing") and
  (.result.state == "capturing") and
  (.result.ipc_version == "1.0.0")
' >/dev/null

capturing_ipc_state="$(printf '%s\n' "${ipc_capturing_json}" | jq -r '.daemon_state // .result.state')"
capturing_cli_state="$(printf '%s\n' "${cli_capturing_json}" | jq -r '.result.state')"
if [[ "${capturing_ipc_state}" != "${capturing_cli_state}" ]]; then
  echo "ERR_DAEMON_STATUS_TRUTH_MISMATCH phase=capturing ipc=${capturing_ipc_state} cli=${capturing_cli_state}" >&2
  exit 1
fi

capture_end_json="$(ipc_request_json "${SOCKET}" "daemon.capture.end" "task4-capture-end")"
printf '%s\n' "${capture_end_json}" | jq -e '.ok==true and ((.daemon_state // .result.state) == "ready")' >/dev/null

printf 'ok daemon_status_ipc_truth socket=%s\n' "${SOCKET}" >"${EVIDENCE_OK}"
printf 'ready.ipc=%s\n' "${ready_ipc_state}" >>"${EVIDENCE_OK}"
printf 'ready.cli=%s\n' "${ready_cli_state}" >>"${EVIDENCE_OK}"
printf 'capturing.ipc=%s\n' "${capturing_ipc_state}" >>"${EVIDENCE_OK}"
printf 'capturing.cli=%s\n' "${capturing_cli_state}" >>"${EVIDENCE_OK}"

python3 - "${ORPHAN_SOCKET}" <<'PY' &
import os
import socket
import sys
import time

path = sys.argv[1]
try:
    os.unlink(path)
except FileNotFoundError:
    pass

server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(1)

conn, _ = server.accept()
conn.settimeout(0.25)
try:
    conn.recv(4096)
except Exception:
    pass
time.sleep(0.05)
conn.close()
server.close()
PY
ORPHAN_PID=$!

for _ in $(seq 1 20); do
  if [[ -S "${ORPHAN_SOCKET}" ]]; then
    break
  fi
  sleep 0.01
done

set +e
orphan_json="$(SHAULA_SOCKET="${ORPHAN_SOCKET}" ./zig-out/bin/shaula daemon status --json 2>&1)"
orphan_rc=$?
set -e

if [[ -n "${ORPHAN_PID}" ]]; then
  wait "${ORPHAN_PID}" >/dev/null 2>&1 || true
  ORPHAN_PID=""
fi

printf '%s\n' "${orphan_json}" >"${EVIDENCE_ERR}"

if [[ ${orphan_rc} -eq 0 ]]; then
  echo "ERR_DAEMON_STATUS_TRUTH_INVALID reason=orphan_socket_unexpected_success" >&2
  exit 1
fi

printf '%s\n' "${orphan_json}" | jq -e '
  .ok==false and
  (.error.code=="ERR_DAEMON_NOT_RUNNING" or .error.code=="ERR_IPC_TIMEOUT")
' >/dev/null || {
  echo "ERR_DAEMON_STATUS_TRUTH_INVALID reason=orphan_socket_error_token_mismatch" >&2
  printf '%s\n' "${orphan_json}" >&2
  exit 1
}

echo "ok daemon_status_ipc_truth"
