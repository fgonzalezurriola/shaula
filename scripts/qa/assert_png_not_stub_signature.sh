#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: scripts/qa/assert_png_not_stub_signature.sh <png-path>" >&2
  exit 2
fi

png_path="$1"
if [[ ! -f "${png_path}" ]]; then
  echo "ERR_CAPTURE_PNG_NOT_FOUND path=${png_path}" >&2
  exit 1
fi

stub_hex="89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000000020001e527d4a20000000049454e44ae426082"
actual_hex="$(head -c 67 "${png_path}" | od -An -tx1 -v | tr -d ' \n')"

if [[ "${actual_hex}" == "${stub_hex}" ]]; then
  echo "ERR_CAPTURE_STUB_SIGNATURE_DETECTED path=${png_path} signature=stub-v1x1" >&2
  exit 1
fi

bytes="$(wc -c < "${png_path}" | tr -d ' ')"
echo "ok capture_png_not_stub_signature path=${png_path} bytes=${bytes}"
