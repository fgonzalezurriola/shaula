#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

./dev build >/dev/null

test_home="/tmp/shaula-home"
rm -rf "${test_home}"
mkdir -p "${test_home}"

default_json="$(HOME="${test_home}" SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture fullscreen --json --no-preview --save)"

default_path="$(printf '%s\n' "${default_json}" | jq -r '.path')"

printf '%s\n' "${default_json}" | jq -e --arg pfx "${test_home}/Pictures/shaula/" '
  .ok == true and
  (.path | startswith($pfx)) and
  (.path | test("/[0-9]{8}-[0-9]{6}(-[0-9]+)?\\.png$"))
' >/dev/null || {
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=default_path_contract" >&2
  printf '%s\n' "${default_json}" >&2
  exit 1
}

[[ -f "${default_path}" ]] || {
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=default_file_missing" >&2
  exit 1
}

set +e
missing_home_json="$(env -u HOME SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture fullscreen --json --save 2>/tmp/shaula-default-output-missing-home.err)"
missing_home_rc=$?
set -e

if [[ ${missing_home_rc} -eq 0 ]]; then
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=missing_home_should_fail" >&2
  exit 1
fi

printf '%s\n' "${missing_home_json}" | jq -e '
  .ok == false and
  .error.code == "ERR_OUTPUT_PATH_INVALID"
' >/dev/null || {
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=missing_home_code_mismatch" >&2
  printf '%s\n' "${missing_home_json}" >&2
  exit 1
}

file_home="/tmp/shaula-home-not-dir"
rm -f "${file_home}"
touch "${file_home}"

set +e
invalid_home_json="$(HOME="${file_home}" SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture fullscreen --json --save 2>/tmp/shaula-default-output-invalid-home.err)"
invalid_home_rc=$?
set -e

if [[ ${invalid_home_rc} -eq 0 ]]; then
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=invalid_home_should_fail" >&2
  exit 1
fi

printf '%s\n' "${invalid_home_json}" | jq -e '
  .ok == false and
  .error.code == "ERR_OUTPUT_PATH_INVALID"
' >/dev/null || {
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=invalid_home_code_mismatch" >&2
  printf '%s\n' "${invalid_home_json}" >&2
  exit 1
}

explicit_json="$(SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./build/shaula capture area --json --no-preview --output /tmp/explicit-path.png)"

printf '%s\n' "${explicit_json}" | jq -e '
  .ok == true and
  .path == "/tmp/explicit-path.png"
' >/dev/null || {
  echo "ERR_DEFAULT_OUTPUT_PATH_INVALID reason=explicit_output_regressed" >&2
  printf '%s\n' "${explicit_json}" >&2
  exit 1
}

echo "ok default_output_path_pictures_shaula"
