#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing overlay helper contract v1 (ok)"
OUTPUT=$(SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=ok ./zig-out/bin/shaula capture area --dry-run --json)

if echo "$OUTPUT" | jq -e '.ok==true and .selection.cancelled==false and .selection.geometry.x==320 and .selection.geometry.y==180 and .selection.geometry.width==640 and .selection.geometry.height==360' >/dev/null; then
  echo "PASS: Helper ok payload mapped to SelectionResult geometry"
  echo "$OUTPUT" | jq .

  mkdir -p .sisyphus/evidence
  echo "$OUTPUT" > .sisyphus/evidence/task-2-overlay-contract-ok.json
  exit 0
else
  echo "FAIL: Helper ok payload did not map deterministically"
  echo "$OUTPUT"
  exit 1
fi
