#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build >/dev/null

SOCKET="${SHAULA_SOCKET:-/tmp/shaula-task6-lifecycle.sock}"

start_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon start --json)"
printf '%s\n' "${start_json}" | jq -e '.status=="ready" and .result.state=="ready"' >/dev/null

status_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon status --json)"
printf '%s\n' "${status_json}" | jq -e '.state|IN("ready","degraded")' >/dev/null

stop_json="$(SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json)"
printf '%s\n' "${stop_json}" | jq -e '.stopped==true' >/dev/null

if [[ -S "${SOCKET}" ]]; then
  echo "ERR_DAEMON_STOP_INVALID reason=socket_still_present_after_stop socket=${SOCKET}" >&2
  exit 1
fi

echo "ok daemon_lifecycle socket=${SOCKET}"
