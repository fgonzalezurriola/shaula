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

INJECT_MARKER=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --inject-known-marker)
      INJECT_MARKER=1
      shift
      ;;
    *)
      echo "ERR_SHELL_ARTIFACT_GUARD_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

zig build >/dev/null

mkdir -p "${ROOT_DIR}/.qa/evidence" /tmp/shaula

token_file="/tmp/shaula/task8-panel-hidden.token"
capture_path="/tmp/shaula/task8-shell-artifact-guard.png"
rm -f "${token_file}" "${capture_path}"

# Simulate shell-side panel-hidden handshake arriving shortly after trigger.
python3 - "${token_file}" <<'PY' &
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
path.unlink(missing_ok=True)
time.sleep(0.015)
path.write_text("hidden", encoding="utf-8")
PY
token_writer_pid=$!

capture_json="$({
  SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_PANEL_HIDDEN_TOKEN_FILE="${token_file}" \
  SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS=200 \
  SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS=120 \
  SHAULA_CAPTURE_SETTLE_BARRIER_MS=30 \
  SHAULA_CAPTURE_INJECT_PANEL_MARKER="${INJECT_MARKER}" \
  SHAULA_PANEL_HIDDEN=0 \
  ./zig-out/bin/shaula capture area --json --no-preview --output "${capture_path}"
} )"

wait "${token_writer_pid}"

printf '%s\n' "${capture_json}" | jq -e --arg path "${capture_path}" '
  .ok == true and
  .mode == "area" and
  .path == $path and
  (.warnings | index("capture_precondition_panel_hidden_handshake") != null)
' >/dev/null || {
  echo "ERR_SHELL_ARTIFACT_GUARD_INVALID reason=handshake_contract" >&2
  printf '%s\n' "${capture_json}" >&2
  exit 1
}

[[ -f "${capture_path}" ]] || {
  echo "ERR_SHELL_ARTIFACT_GUARD_INVALID reason=capture_file_missing" >&2
  exit 1
}

marker_absent_json="$(python3 - "${capture_path}" <<'PY'
import json
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAGENTA = (255, 0, 255, 255)

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

def decode(path: str) -> tuple[int, int, bytes]:
    with open(path, "rb") as fh:
        data = fh.read()
    if not data.startswith(SIGNATURE):
        raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=invalid_signature")

    offset = len(SIGNATURE)
    width = height = None
    idat = bytearray()
    while offset < len(data):
        if offset + 8 > len(data):
            raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=truncated_header")
        size = struct.unpack(">I", data[offset : offset + 4])[0]
        ctype = data[offset + 4 : offset + 8]
        offset += 8
        if offset + size + 4 > len(data):
            raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=truncated_chunk")
        payload = data[offset : offset + size]
        offset += size + 4
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, comp, flt, interlace = struct.unpack(">IIBBBBB", payload)
            if (bit_depth, color_type, comp, flt, interlace) != (8, 6, 0, 0, 0):
                raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=unsupported_format")
        elif ctype == b"IDAT":
            idat.extend(payload)
        elif ctype == b"IEND":
            break
    if width is None or height is None:
        raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=missing_ihdr")
    raw = zlib.decompress(bytes(idat))
    return width, height, raw

def has_magenta_marker(path: str) -> bool:
    width, height, raw = decode(path)
    stride = width * 4
    prev = bytearray(stride)
    cursor = 0
    marker_pixels = 0

    for y in range(height):
        f = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride

        if f == 1:
            for i in range(stride):
                left = row[i - 4] if i >= 4 else 0
                row[i] = (row[i] + left) & 0xFF
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = row[i - 4] if i >= 4 else 0
                up = prev[i]
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
        elif f == 4:
            for i in range(stride):
                left = row[i - 4] if i >= 4 else 0
                up = prev[i]
                ul = prev[i - 4] if i >= 4 else 0
                row[i] = (row[i] + paeth(left, up, ul)) & 0xFF
        elif f != 0:
            raise SystemExit("ERR_CAPTURE_CONTENT_PNG_DECODE failure=unsupported_filter")

        if y < 16:
            for x in range(0, min(16, width)):
                base = x * 4
                px = (row[base], row[base + 1], row[base + 2], row[base + 3])
                if px == MAGENTA:
                    marker_pixels += 1

        prev[:] = row

    return marker_pixels > 0

print(json.dumps({"marker_present": has_magenta_marker(sys.argv[1])}))
PY
)"

printf '%s\n' "${marker_absent_json}" | jq -e '.marker_present == false' >/dev/null || {
  echo "ERR_SHELL_ARTIFACT_GUARD_INVALID reason=marker_leaked_into_capture" >&2
  printf '%s\n' "${marker_absent_json}" >&2
  exit 1
}

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
evidence_json="${ROOT_DIR}/.qa/evidence/task-8-shell-artifact-guard.json"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson capture "${capture_json}" \
  --argjson marker "${marker_absent_json}" \
  '{
    suite: "task-8-shell-artifact-guard",
    timestamp: $timestamp,
    pass: true,
    capture: {
      mode: $capture.mode,
      path: $capture.path,
      warnings: $capture.warnings
    },
    checks: {
      handshake_warning_present: {
        pass: (($capture.warnings | index("capture_precondition_panel_hidden_handshake")) != null)
      },
      panel_marker_absent: {
        pass: ($marker.marker_present == false)
      }
    }
  }' > "${evidence_json}"

echo "ok shell_artifact_guard evidence=${evidence_json}"
