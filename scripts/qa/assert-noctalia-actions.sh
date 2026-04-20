#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

MODE="with-plugin"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-plugin)
      MODE="with-plugin"
      shift
      ;;
    *)
      echo "ERR_NOCTALIA_ACTION_ASSERT_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

if [[ "${MODE}" != "with-plugin" ]]; then
  echo "ERR_NOCTALIA_ACTION_ASSERT_USAGE reason=unsupported_mode mode=${MODE}" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=python3" >&2
  exit 1
fi

PLUGIN_SCRIPT="${ROOT_DIR}/integrations/noctalia/noctalia-plugin-poc.sh"
ADAPTER_SCRIPT="${ROOT_DIR}/integrations/noctalia/noctalia-action-adapter.sh"
HELPER_SCRIPT="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
EVIDENCE_PATH="${ROOT_DIR}/.sisyphus/evidence/task-9-noctalia-actions.json"

if [[ ! -f "${PLUGIN_SCRIPT}" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-plugin-poc.sh" >&2
  exit 1
fi

if [[ ! -f "${ADAPTER_SCRIPT}" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_MISSING path=integrations/noctalia/noctalia-action-adapter.sh" >&2
  exit 1
fi

if [[ ! -x "${PLUGIN_SCRIPT}" ]]; then
  chmod +x "${PLUGIN_SCRIPT}"
fi

if [[ ! -x "${ADAPTER_SCRIPT}" ]]; then
  chmod +x "${ADAPTER_SCRIPT}"
fi

if [[ ! -x "${HELPER_SCRIPT}" ]]; then
  chmod +x "${HELPER_SCRIPT}"
fi

zig build >/dev/null

mkdir -p "${ROOT_DIR}/.sisyphus/evidence" /tmp/shaula /tmp/shaula-task9-home

EXPECTED_ACTIONS='[
  {"id":"capture-area","label":"Capture Area","shaula_argv":["capture","area","--json"]},
  {"id":"capture-fullscreen","label":"Capture Fullscreen","shaula_argv":["capture","fullscreen","--json"]},
  {"id":"capture-window","label":"Capture Window","shaula_argv":["capture","window","--json"]},
  {"id":"open-last","label":"Open Last","shaula_argv":["history","show","--json","--id","latest"]},
  {"id":"history","label":"History","shaula_argv":["history","list","--json"]}
]'

EXPECTED_ACTION_MAP='{
  "capture-area": {"label":"Capture Area","shaula_argv":["capture","area","--json"]},
  "capture-fullscreen": {"label":"Capture Fullscreen","shaula_argv":["capture","fullscreen","--json"]},
  "capture-window": {"label":"Capture Window","shaula_argv":["capture","window","--json"]},
  "open-last": {"label":"Open Last","shaula_argv":["history","show","--json","--id","latest"]},
  "history": {"label":"History","shaula_argv":["history","list","--json"]}
}'

menu_json="$(${PLUGIN_SCRIPT} --menu --request-id "task9-menu")"
printf '%s\n' "${menu_json}" | jq -e --argjson expected_actions "${EXPECTED_ACTIONS}" '
  .ok == true and
  .plugin == "noctalia" and
  .optional == true and
  .menu.minimal == true and
  .menu.actions == $expected_actions
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=menu_contract_invalid" >&2
  printf '%s\n' "${menu_json}" >&2
  exit 1
}

for action in capture-area capture-fullscreen capture-window open-last history; do
  dry_json="$(${PLUGIN_SCRIPT} --action "${action}" --dry-run --request-id "task9-dry-${action}")"
  printf '%s\n' "${dry_json}" | jq -e --arg action "${action}" --argjson expected "${EXPECTED_ACTION_MAP}" '
    .ok == true and
    .plugin == "noctalia" and
    .optional == true and
    .execution.mode == "dry-run" and
    .action.id == $action and
    .action.label == $expected[$action].label and
    .action.shaula_argv == $expected[$action].shaula_argv
  ' >/dev/null || {
    echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=dry_run_mapping_invalid action=${action}" >&2
    printf '%s\n' "${dry_json}" >&2
    exit 1
  }
done

area_exec_json="$({
  HOME=/tmp/shaula-task9-home \
  SHAULA_RUNTIME_CAPTURE_HELPER="${HELPER_SCRIPT}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  ${PLUGIN_SCRIPT} --action capture-area --request-id "task9-exec-capture-area"
})"

printf '%s\n' "${area_exec_json}" | jq -e '
  .ok == true and
  .action.id == "capture-area" and
  .action.shaula_argv == ["capture","area","--json"] and
  .execution.mode == "execute" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.execution.output | fromjson | .ok == true and .mode == "area" and .mime == "image/png")
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_area_invalid" >&2
  printf '%s\n' "${area_exec_json}" >&2
  exit 1
}

area_capture_path="$(printf '%s\n' "${area_exec_json}" | jq -r '.execution.output | fromjson | .path')"
[[ -f "${area_capture_path}" ]] || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_area_file_missing path=${area_capture_path}" >&2
  exit 1
}

fullscreen_exec_json="$({
  HOME=/tmp/shaula-task9-home \
  SHAULA_RUNTIME_CAPTURE_HELPER="${HELPER_SCRIPT}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  ${PLUGIN_SCRIPT} --action capture-fullscreen --request-id "task9-exec-capture-fullscreen"
})"

printf '%s\n' "${fullscreen_exec_json}" | jq -e '
  .ok == true and
  .action.id == "capture-fullscreen" and
  .action.shaula_argv == ["capture","fullscreen","--json"] and
  .execution.mode == "execute" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.execution.output | fromjson | .ok == true and .mode == "fullscreen" and .mime == "image/png")
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_fullscreen_invalid" >&2
  printf '%s\n' "${fullscreen_exec_json}" >&2
  exit 1
}

fullscreen_capture_path="$(printf '%s\n' "${fullscreen_exec_json}" | jq -r '.execution.output | fromjson | .path')"
[[ -f "${fullscreen_capture_path}" ]] || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_fullscreen_file_missing path=${fullscreen_capture_path}" >&2
  exit 1
}

set +e
window_exec_json="$({
  HOME=/tmp/shaula-task9-home \
  SHAULA_RUNTIME_CAPTURE_HELPER="${HELPER_SCRIPT}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  ${PLUGIN_SCRIPT} --action capture-window --request-id "task9-exec-capture-window"
})"
window_rc=$?
set -e

if [[ ${window_rc} -eq 0 ]]; then
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_window_unexpected_success" >&2
  printf '%s\n' "${window_exec_json}" >&2
  exit 1
fi

printf '%s\n' "${window_exec_json}" | jq -e '
  .ok == false and
  .action.id == "capture-window" and
  .action.shaula_argv == ["capture","window","--json"] and
  .execution.mode == "execute" and
  .execution.ok == false and
  (.execution.exit_code > 0) and
  (.execution.output | fromjson | .ok == false and .error.code == "ERR_CAPTURE_MODE_UNSUPPORTED")
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_capture_window_contract_invalid" >&2
  printf '%s\n' "${window_exec_json}" >&2
  exit 1
}

open_last_exec_json="$(${PLUGIN_SCRIPT} --action open-last --request-id "task9-exec-open-last")"
printf '%s\n' "${open_last_exec_json}" | jq -e '
  .ok == true and
  .action.id == "open-last" and
  .action.shaula_argv == ["history","show","--json","--id","latest"] and
  .execution.mode == "execute" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.execution.output | fromjson | .ok == true and .command == "history show" and .result.id == "latest")
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_open_last_invalid" >&2
  printf '%s\n' "${open_last_exec_json}" >&2
  exit 1
}

history_exec_json="$(${PLUGIN_SCRIPT} --action history --request-id "task9-exec-history")"
printf '%s\n' "${history_exec_json}" | jq -e '
  .ok == true and
  .action.id == "history" and
  .action.shaula_argv == ["history","list","--json"] and
  .execution.mode == "execute" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.execution.output | fromjson | .ok == true and .command == "history list" and (.result.entries | length >= 1))
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_history_invalid" >&2
  printf '%s\n' "${history_exec_json}" >&2
  exit 1
}

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson menu "${menu_json}" \
  --argjson area_exec "${area_exec_json}" \
  --argjson fullscreen_exec "${fullscreen_exec_json}" \
  --argjson window_exec "${window_exec_json}" \
  --argjson open_last_exec "${open_last_exec_json}" \
  --argjson history_exec "${history_exec_json}" \
  '{
    suite: "task-9-noctalia-actions",
    timestamp: $timestamp,
    pass: true,
    plugin_optional: true,
    adapter_deterministic: true,
    menu: {
      minimal: $menu.menu.minimal,
      actions: $menu.menu.actions
    },
    checks: {
      menu_contract: { pass: true },
      dry_run_mapping: { pass: true },
      execute_capture_area: {
        pass: true,
        exit_code: $area_exec.execution.exit_code,
        action: $area_exec.action.id
      },
      execute_capture_fullscreen: {
        pass: true,
        exit_code: $fullscreen_exec.execution.exit_code,
        action: $fullscreen_exec.action.id
      },
      execute_capture_window_expected_unsupported: {
        pass: true,
        exit_code: $window_exec.execution.exit_code,
        action: $window_exec.action.id
      },
      execute_open_last: {
        pass: true,
        exit_code: $open_last_exec.execution.exit_code,
        action: $open_last_exec.action.id
      },
      execute_history: {
        pass: true,
        exit_code: $history_exec.execution.exit_code,
        action: $history_exec.action.id
      }
    }
  }' > "${EVIDENCE_PATH}"

echo "ok noctalia_actions evidence=${EVIDENCE_PATH}"
