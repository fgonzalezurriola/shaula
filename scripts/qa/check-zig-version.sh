#!/usr/bin/env bash
set -euo pipefail

expected_zig="${SHAULA_EXPECTED_ZIG:-0.16.0}"

if ! command -v zig >/dev/null 2>&1; then
  echo "ERR_TOOLCHAIN_ZIG_MISSING expected=${expected_zig} actual=missing" >&2
  exit 1
fi

actual_zig="$(zig version 2>/dev/null || true)"

if [ "${actual_zig}" != "${expected_zig}" ]; then
  echo "ERR_TOOLCHAIN_VERSION_MISMATCH expected=${expected_zig} actual=${actual_zig}" >&2
  exit 1
fi

echo "ok zig=${actual_zig}"
exit 0
