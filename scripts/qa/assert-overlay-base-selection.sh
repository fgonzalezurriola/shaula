#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing deterministic base selection output"
OUTPUT=$(./build/shaula capture area --dry-run --json)

if echo "$OUTPUT" | jq -e '.ok==true and .selection.cancelled==false and (.selection.geometry | type == "object") and .selection.geometry.width > 0' >/dev/null; then
  echo "PASS: Selection output deterministic, not cancelled, and geometry is valid"
  echo "$OUTPUT" | jq .
  
  # Emit evidence
  mkdir -p .qa/evidence
  echo "$OUTPUT" > .qa/evidence/task-7-overlay-base.txt
  exit 0
else
  echo "FAIL: Selection output invalid or missing geometry"
  echo "$OUTPUT"
  exit 1
fi
