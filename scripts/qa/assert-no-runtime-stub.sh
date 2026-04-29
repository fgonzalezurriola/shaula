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

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

zig build >/dev/null

capture_path="/tmp/shaula/qa-runtime-capture.png"
forced_stub_path="/tmp/shaula/qa-forced-stub-should-not-exist.png"

rm -f "${capture_path}" "${forced_stub_path}"

capture_json="$(SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output "${capture_path}")"

printf '%s\n' "${capture_json}" | jq -e --arg capture_path "${capture_path}" '
  .ok == true and
  .mode == "area" and
  .mime == "image/png" and
  .path == $capture_path and
  (.backend_used | type == "string" and length > 0)
' >/dev/null || {
  echo "ERR_CAPTURE_RUNTIME_BACKEND_INVALID reason=runtime_contract" >&2
  exit 1
}

[[ -f "${capture_path}" ]] || {
  echo "ERR_CAPTURE_RUNTIME_BACKEND_INVALID reason=runtime_file_missing" >&2
  exit 1
}

bash scripts/qa/assert_png_not_stub_signature.sh "${capture_path}" >/dev/null

set +e
forced_json="$(SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_CAPTURE_BACKEND=__stub__ SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output "${forced_stub_path}" 2>/tmp/shaula-qa-runtime-stub.err)"
forced_rc=$?
set -e

if [[ ${forced_rc} -eq 0 ]]; then
  echo "ERR_CAPTURE_RUNTIME_BACKEND_INVALID reason=forced_stub_should_fail" >&2
  exit 1
fi

printf '%s\n' "${forced_json}" | jq -e '
  .ok == false and
  .error.code == "ERR_CAPTURE_BACKEND_UNAVAILABLE" and
  .backend_used == "__stub__"
' >/dev/null || {
  echo "ERR_CAPTURE_RUNTIME_BACKEND_INVALID reason=forced_stub_error_contract" >&2
  printf '%s\n' "${forced_json}" >&2
  exit 1
}

if [[ -e "${forced_stub_path}" ]]; then
  echo "ERR_CAPTURE_RUNTIME_BACKEND_INVALID reason=forced_stub_output_written" >&2
  exit 1
fi

echo "ok capture_runtime_backend_non_stub"
