#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing deterministic selection drag interaction"
OUTPUT=$(SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=interaction_drag ./zig-out/bin/shaula capture area --dry-run --json)

if echo "$OUTPUT" | jq -e '.ok==true and .selection.cancelled==false and .selection.geometry.width==400 and .selection.geometry.height==300 and .selection.geometry.x==100 and .selection.geometry.y==100' >/dev/null; then
  echo "PASS: Drag interaction emitted deterministic geometry"
  mkdir -p .sisyphus/evidence
  echo "$OUTPUT" > .sisyphus/evidence/task-7-selection-drag.json
  exit 0
else
  echo "FAIL: Drag interaction did not emit expected geometry"
  echo "$OUTPUT"
  exit 1
fi
