#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing deterministic selection cancel interaction"
OUTPUT=$(SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=interaction_cancel ./zig-out/bin/shaula capture area --dry-run --json || true)

if echo "$OUTPUT" | grep -q 'ERR_SELECTION_CANCELLED'; then
  echo "PASS: Esc interaction mapped to deterministic cancellation"
  mkdir -p .qa/evidence
  echo "$OUTPUT" > .qa/evidence/task-7-selection-cancel.json
  exit 0
else
  echo "FAIL: Esc interaction did not trigger deterministic cancellation"
  echo "$OUTPUT"
  exit 1
fi
