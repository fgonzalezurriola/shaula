#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

fixture="colorful-grid"
mode="fullscreen"
non_black_threshold="35"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fixture)
      fixture="$2"
      shift 2
      ;;
    --mode)
      mode="$2"
      shift 2
      ;;
    --non-black-threshold)
      non_black_threshold="$2"
      shift 2
      ;;
    *)
      echo "usage: bash scripts/qa/assert-capture-content-validity.sh [--fixture <name>] [--mode <area|fullscreen|window>] [--non-black-threshold <0-255>]" >&2
      exit 2
      ;;
  esac
done

case "${mode}" in
  area|fullscreen|window) ;;
  *)
    echo "ERR_CAPTURE_CONTENT_INVALID reason=unsupported_mode mode=${mode}" >&2
    exit 1
    ;;
esac

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

evidence_dir="${ROOT_DIR}/.qa/evidence"
evidence_json="${evidence_dir}/task-3-capture-content-validity.json"

mkdir -p "${evidence_dir}" /tmp/shaula

zig build >/dev/null

capture_path="/tmp/shaula/task3-capture-content-${mode}.png"
stub_path="/tmp/shaula/task3-stub-signature-1x1.png"

rm -f "${capture_path}" "${stub_path}"

capture_json="$(SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_CAPTURE_FIXTURE="${fixture}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture "${mode}" --json --output "${capture_path}")"

printf '%s\n' "${capture_json}" | jq -e --arg mode "${mode}" --arg capture_path "${capture_path}" '
  .ok == true and
  .mode == $mode and
  .mime == "image/png" and
  .path == $capture_path and
  (.dimensions.width > 0 and .dimensions.height > 0) and
  (.backend_used | type == "string" and length > 0)
' >/dev/null || {
  echo "ERR_CAPTURE_CONTENT_INVALID reason=capture_contract" >&2
  exit 1
}

[[ -f "${capture_path}" ]] || {
  echo "ERR_CAPTURE_CONTENT_INVALID reason=capture_file_missing" >&2
  exit 1
}

decoded_json="$(python3 - "${capture_path}" <<'PY'
import json
import struct
import sys
import zlib


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path: str) -> dict[str, float | int]:
    with open(path, "rb") as fh:
        data = fh.read()

    signature = b"\x89PNG\r\n\x1a\n"
    if not data.startswith(signature):
        raise ValueError("ERR_CAPTURE_PNG_INVALID_SIGNATURE")

    offset = len(signature)
    width = None
    height = None
    bit_depth = None
    color_type = None
    idat = bytearray()

    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("ERR_CAPTURE_PNG_TRUNCATED_CHUNK_HEADER")

        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        offset += 8

        if offset + length + 4 > len(data):
            raise ValueError("ERR_CAPTURE_PNG_TRUNCATED_CHUNK_DATA")

        payload = data[offset : offset + length]
        offset += length
        offset += 4

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, flt, interlace = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or flt != 0 or interlace != 0:
                raise ValueError("ERR_CAPTURE_PNG_UNSUPPORTED_FORMAT")
            if bit_depth != 8 or color_type != 6:
                raise ValueError("ERR_CAPTURE_PNG_UNSUPPORTED_COLOR_TYPE")
        elif chunk_type == b"IDAT":
            idat.extend(payload)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None:
        raise ValueError("ERR_CAPTURE_PNG_MISSING_IHDR")

    if not idat:
        raise ValueError("ERR_CAPTURE_PNG_MISSING_IDAT")

    raw = zlib.decompress(bytes(idat))
    bytes_per_pixel = 4
    stride = width * bytes_per_pixel
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError("ERR_CAPTURE_PNG_DECODE_SIZE_MISMATCH")

    prev = bytearray(stride)
    cursor = 0
    luma_sum = 0.0
    pixels = width * height

    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1

        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride

        if filter_type == 1:
            for i in range(stride):
                left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                row[i] = (row[i] + left) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                up = prev[i]
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                up = prev[i]
                up_left = prev[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
                row[i] = (row[i] + paeth(left, up, up_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError("ERR_CAPTURE_PNG_UNSUPPORTED_FILTER")

        for x in range(0, stride, bytes_per_pixel):
            r = row[x]
            g = row[x + 1]
            b = row[x + 2]
            luma_sum += (0.2126 * r) + (0.7152 * g) + (0.0722 * b)

        prev[:] = row

    avg_luma = luma_sum / pixels if pixels > 0 else 0.0
    return {
        "width": width,
        "height": height,
        "avg_luma": avg_luma,
    }


try:
    result = decode_png(sys.argv[1])
except Exception as exc:  # noqa: BLE001
    print(f"ERR_CAPTURE_CONTENT_PNG_DECODE failure={exc}", file=sys.stderr)
    raise SystemExit(1)

print(json.dumps(result))
PY
)"

printf '%s\n' "${decoded_json}" | jq -e --argjson capture "${capture_json}" '
  .width == $capture.dimensions.width and
  .height == $capture.dimensions.height
' >/dev/null || {
  echo "ERR_CAPTURE_CONTENT_INVALID reason=decoded_dimensions_mismatch" >&2
  printf '%s\n' "capture_json=${capture_json}" >&2
  printf '%s\n' "decoded_json=${decoded_json}" >&2
  exit 1
}

python3 scripts/qa/assert_png_not_stub_signature.py "${capture_path}" >/dev/null

python3 - "${stub_path}" <<'PY'
import importlib.util
import pathlib
import sys

script_path = pathlib.Path("scripts/qa/assert_png_not_stub_signature.py")
spec = importlib.util.spec_from_file_location("stub_checker", script_path)
if spec is None or spec.loader is None:
    raise SystemExit("ERR_CAPTURE_CONTENT_INVALID reason=stub_loader_unavailable")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
pathlib.Path(sys.argv[1]).write_bytes(module.STUB_SIGNATURE)
PY

set +e
stub_reject_output="$(python3 scripts/qa/assert_png_not_stub_signature.py "${stub_path}" 2>&1)"
stub_reject_rc=$?
set -e

if [[ ${stub_reject_rc} -eq 0 ]]; then
  echo "ERR_CAPTURE_CONTENT_INVALID reason=stub_signature_not_rejected" >&2
  exit 1
fi

[[ "${stub_reject_output}" == *"ERR_CAPTURE_STUB_SIGNATURE_DETECTED"* ]] || {
  echo "ERR_CAPTURE_CONTENT_INVALID reason=stub_rejection_error_token_missing" >&2
  printf '%s\n' "${stub_reject_output}" >&2
  exit 1
}

non_black_check_applied=false
non_black_pass=true
avg_luma="$(printf '%s\n' "${decoded_json}" | jq -r '.avg_luma')"

if [[ "${fixture}" == "colorful-grid" ]]; then
  non_black_check_applied=true
  non_black_pass="$(printf '%s\n' "${decoded_json}" | jq -r --argjson threshold "${non_black_threshold}" '.avg_luma > $threshold')"
  if [[ "${non_black_pass}" != "true" ]]; then
    echo "ERR_CAPTURE_CONTENT_INVALID reason=fixture_non_black_threshold fixture=${fixture} avg_luma=${avg_luma} threshold=${non_black_threshold}" >&2
    exit 1
  fi
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --arg fixture "${fixture}" \
  --arg mode "${mode}" \
  --arg path "${capture_path}" \
  --argjson capture "${capture_json}" \
  --argjson decoded "${decoded_json}" \
  --arg stub_rejection_output "${stub_reject_output}" \
  --argjson non_black_threshold "${non_black_threshold}" \
  --argjson non_black_check_applied "${non_black_check_applied}" \
  --argjson non_black_pass "${non_black_pass}" \
  '{
    suite: "task-3-capture-content-validity",
    timestamp: $timestamp,
    pass: true,
    fixture: $fixture,
    mode: $mode,
    capture: {
      path: $path,
      mime: $capture.mime,
      backend_used: $capture.backend_used,
      json_dimensions: $capture.dimensions,
      decoded_dimensions: {
        width: $decoded.width,
        height: $decoded.height
      }
    },
    checks: {
      png_decode: { pass: true },
      dimensions_match: { pass: true },
      stub_signature_rejected: {
        pass: true,
        token: "ERR_CAPTURE_STUB_SIGNATURE_DETECTED",
        output: $stub_rejection_output
      },
      fixture_non_black: {
        applied: $non_black_check_applied,
        threshold: (if $non_black_check_applied then $non_black_threshold else null end),
        avg_luma: $decoded.avg_luma,
        pass: (if $non_black_check_applied then $non_black_pass else true end)
      }
    }
  }' > "${evidence_json}"

echo "ok capture_content_validity fixture=${fixture} mode=${mode} evidence=${evidence_json}"
