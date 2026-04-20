#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

bash ./scripts/qa/assert-preflight-schema.sh
bash ./scripts/qa/test-failure-matrix.sh
bash ./scripts/qa/assert-exit-code-mapping.sh

echo "ok qa_unit_tests"
