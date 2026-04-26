#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

echo "Testing overlay helper contract v1 (malformed payload)"
OUTPUT=$(SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=malformed ./zig-out/bin/shaula capture area --dry-run --json || true)

if echo "$OUTPUT" | jq -e '.ok==false and .error.code=="ERR_OVERLAY_PROTOCOL_INVALID" and .error.details.mode=="area" and (.warnings|type=="array")' >/dev/null; then
  echo "PASS: Malformed helper payload mapped deterministically to overlay protocol invalid"
  echo "$OUTPUT" | jq .

  mkdir -p .qa/evidence
  echo "$OUTPUT" > .qa/evidence/task-2-overlay-contract-error.txt
  exit 0
else
  echo "FAIL: Malformed payload did not trigger deterministic overlay protocol mapping"
  echo "$OUTPUT"
  exit 1
fi
