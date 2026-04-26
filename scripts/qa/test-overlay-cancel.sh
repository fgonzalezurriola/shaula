#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing overlay cancellation (simulated Esc)"
OUTPUT=$(./zig-out/bin/shaula capture area --dry-run --json --simulate-cancel || true)

if echo "$OUTPUT" | grep -q 'ERR_SELECTION_CANCELLED'; then
  echo "PASS: Cancellation resulted in deterministic error envelope"
  echo "$OUTPUT" | jq .
  
  # Emit evidence
  mkdir -p .qa/evidence
  echo "$OUTPUT" > .qa/evidence/task-7-overlay-base-error.txt
  exit 0
else
  echo "FAIL: Did not get ERR_SELECTION_CANCELLED"
  echo "$OUTPUT"
  exit 1
fi
