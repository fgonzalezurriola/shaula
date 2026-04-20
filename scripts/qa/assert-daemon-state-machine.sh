#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build test >/dev/null
zig build >/dev/null

SOCKET="${SHAULA_SOCKET:-/tmp/shaula-task6-state-machine.sock}"

start_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json)"
printf '%s\n' "${start_json}" | jq -e '.result.state=="ready" and .status=="ready"' >/dev/null

status_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon status --json)"
printf '%s\n' "${status_json}" | jq -e '.state|IN("ready","degraded")' >/dev/null

stop_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json)"
printf '%s\n' "${stop_json}" | jq -e '.stopped==true' >/dev/null

# Deterministic negative check: already running daemon must reject second start.
SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json >/dev/null
set +e
second_start_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json 2>&1)"
second_start_rc=$?
set -e

if [[ ${second_start_rc} -eq 0 ]]; then
  echo "ERR_STATE_MACHINE_INVALID reason=second_start_succeeded" >&2
  SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json >/dev/null || true
  exit 1
fi

printf '%s\n' "${second_start_json}" | jq -e '.error.code=="ERR_DAEMON_ALREADY_RUNNING"' >/dev/null

SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json >/dev/null

# Deterministic bind-failure behavior check.
bind_fail_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon _serve --json --socket /proc/1/invalid.sock 2>/dev/null || true)"
printf '%s\n' "${bind_fail_json}" | jq -e '.error.code=="ERR_IPC_BIND_FAILED"' >/dev/null

echo "ok daemon_state_machine transitions=validated"
