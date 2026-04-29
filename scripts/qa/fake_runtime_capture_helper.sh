#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec zig run "${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.zig" -- "$@"
