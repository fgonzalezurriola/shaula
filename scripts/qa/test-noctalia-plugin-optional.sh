#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

MODE="both"
SOCKET="${SHAULA_SOCKET:-/tmp/shaula-task9-noctalia.sock}"
WITH_PLUGIN=0
WITHOUT_PLUGIN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-plugin) MODE="with-plugin"; WITH_PLUGIN=1; shift ;;
    --without-plugin) MODE="without-plugin"; WITHOUT_PLUGIN=1; shift ;;
    --socket) SOCKET="$2"; shift 2 ;;
    *)
      echo "ERR_NOCTALIA_OPTIONAL_TEST_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

if [[ ${WITH_PLUGIN} -eq 1 && ${WITHOUT_PLUGIN} -eq 1 ]]; then
  MODE="both"
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

PLUGIN_SCRIPT="./integrations/noctalia/noctalia-plugin-poc.sh"
ADAPTER_SCRIPT="./integrations/noctalia/noctalia-action-adapter.sh"

if [[ "${MODE}" == "with-plugin" || "${MODE}" == "both" ]]; then
  if [[ ! -x "${PLUGIN_SCRIPT}" ]]; then
    echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-plugin-poc.sh" >&2
    exit 1
  fi

  if [[ ! -x "${ADAPTER_SCRIPT}" ]]; then
    echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-action-adapter.sh" >&2
    exit 1
  fi
fi

zig build >/dev/null

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

cleanup() {
  set +e
  SHAULA_SOCKET="${SOCKET}" ./zig-out/bin/shaula daemon stop --json >/dev/null 2>&1 || true
  set -e
}
trap cleanup EXIT

mkdir -p /tmp/shaula

if [[ "${MODE}" == "with-plugin" || "${MODE}" == "both" ]]; then
  plugin_json="$("${PLUGIN_SCRIPT}" --menu --request-id "task9-plugin-present")"
  printf '%s\n' "${plugin_json}" | jq -e '
    .ok == true and
    .plugin == "noctalia" and
    .optional == true and
    .menu.minimal == true and
    (.menu.actions | length == 8) and
    any(.menu.actions[]; .id == "capture-all-screens" and .shaula_argv == ["capture","all-screens","--json"])
  ' >/dev/null || {
    echo "ERR_NOCTALIA_PLUGIN_FLOW_INVALID reason=plugin_present_contract" >&2
    exit 1
  }

  dry_run_json="$("${PLUGIN_SCRIPT}" --action capture-area --dry-run --request-id "task9-plugin-dry-run")"
  printf '%s\n' "${dry_run_json}" | jq -e '
    .ok == true and
    .plugin == "noctalia" and
    .optional == true and
    .action.id == "capture-area" and
    .execution.mode == "dry-run" and
    .action.shaula_argv == ["capture","area","--json"]
  ' >/dev/null || {
    echo "ERR_NOCTALIA_PLUGIN_FLOW_INVALID reason=plugin_dry_run_contract" >&2
    exit 1
  }
fi

if [[ "${MODE}" == "without-plugin" || "${MODE}" == "both" ]]; then
  CAPTURE_PATH="/tmp/shaula/task9-without-plugin-area.png"
  rm -f "${CAPTURE_PATH}"

  capture_json="$({
    SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
    SHAULA_COMPOSITOR=niri \
    NIRI_SOCKET=/tmp/niri.sock \
    WAYLAND_DISPLAY=wayland-1 \
    ./zig-out/bin/shaula capture area --json --no-preview --output "${CAPTURE_PATH}"
  })"
  printf '%s\n' "${capture_json}" | jq -e '
    .ok == true and
    .mode == "area" and
    .mime == "image/png" and
    (.backend_used | length > 0) and
    (.path | length > 0) and
    (.latency_ms >= 0)
  ' >/dev/null || {
    echo "ERR_NOCTALIA_OPTIONALITY_BROKEN reason=capture_without_plugin_invalid" >&2
    exit 1
  }

  [[ -f "${CAPTURE_PATH}" ]] || {
    echo "ERR_NOCTALIA_OPTIONALITY_BROKEN reason=capture_file_missing_without_plugin" >&2
    exit 1
  }

  set +e
  missing_plugin_output="$({
    SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
    SHAULA_COMPOSITOR=niri \
    NIRI_SOCKET=/tmp/niri.sock \
    WAYLAND_DISPLAY=wayland-1 \
    PATH="/usr/bin:/bin" \
    ./zig-out/bin/shaula capture fullscreen --json 2>&1
  })"
  missing_plugin_rc=$?
  set -e

  if [[ ${missing_plugin_rc} -ne 0 ]]; then
    echo "ERR_NOCTALIA_OPTIONALITY_BROKEN reason=core_capture_depends_on_plugin" >&2
    printf '%s\n' "${missing_plugin_output}" >&2
    exit 1
  fi

  printf '%s\n' "${missing_plugin_output}" | jq -e '
    .ok == true and
    .mode == "fullscreen" and
    .mime == "image/png"
  ' >/dev/null || {
    echo "ERR_NOCTALIA_OPTIONALITY_BROKEN reason=core_capture_without_plugin_invalid" >&2
    printf '%s\n' "${missing_plugin_output}" >&2
    exit 1
  }
fi

echo "PASS_NOCTALIA_OPTIONAL mode=${MODE} socket=${SOCKET}"
