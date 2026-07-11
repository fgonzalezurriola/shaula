#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HELPER="${ROOT_DIR}/build/scripts/qa/shaula-fake-runtime-capture-helper"

if [[ ! -x "${HELPER}" ]]; then
  "${ROOT_DIR}/dev" build >/dev/null
fi

exec "${HELPER}" "$@"
