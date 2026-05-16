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
HELPER_SCRIPT="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.sh"
EVIDENCE_PATH="${ROOT_DIR}/.qa/evidence/task-9-noctalia-actions.json"
OPEN_FOLDER_EVIDENCE_PATH="${ROOT_DIR}/.qa/evidence/task-10-open-folder.json"
OPEN_CLIPBOARD_ERROR_EVIDENCE_PATH="${ROOT_DIR}/.qa/evidence/task-10-open-clipboard-error.json"

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

mkdir -p "${ROOT_DIR}/.qa/evidence" /tmp/shaula /tmp/shaula-task9-home

EXPECTED_ACTIONS='[
  {"id":"capture-area","label":"Quick Capture","shaula_argv":["capture","area","--json"]},
  {"id":"capture-fullscreen","label":"Capture Fullscreen","shaula_argv":["capture","fullscreen","--json"]},
  {"id":"capture-all-screens","label":"Capture All Screens","shaula_argv":["capture","all-screens","--json"]},
  {"id":"capture-window","label":"Capture Window","shaula_argv":["capture","window","--json"]},
  {"id":"open-last","label":"Open Last","shaula_argv":["history","show","--json","--id","latest"]},
  {"id":"history","label":"History","shaula_argv":["history","list","--json"]},
  {"id":"open-output-folder","label":"Open Output Folder","shaula_argv":["helper","open-output-folder"]},
  {"id":"open-clipboard-image","label":"Open Clipboard Image","shaula_argv":["helper","open-clipboard-image"]}
]'

EXPECTED_ACTION_MAP='{
  "capture-area": {"label":"Quick Capture","shaula_argv":["capture","area","--json"]},
  "capture-fullscreen": {"label":"Capture Fullscreen","shaula_argv":["capture","fullscreen","--json"]},
  "capture-all-screens": {"label":"Capture All Screens","shaula_argv":["capture","all-screens","--json"]},
  "capture-window": {"label":"Capture Window","shaula_argv":["capture","window","--json"]},
  "open-last": {"label":"Open Last","shaula_argv":["history","show","--json","--id","latest"]},
  "history": {"label":"History","shaula_argv":["history","list","--json"]},
  "open-output-folder": {"label":"Open Output Folder","shaula_argv":["helper","open-output-folder"]},
  "open-clipboard-image": {"label":"Open Clipboard Image","shaula_argv":["helper","open-clipboard-image"]}
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

for action in capture-area capture-fullscreen capture-all-screens capture-window open-last history open-output-folder open-clipboard-image; do
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

history_seed_path="/tmp/shaula/task9-history-seed.png"
rm -f "${history_seed_path}"

history_seed_json="$({
  HOME=/tmp/shaula-task9-home \
  SHAULA_RUNTIME_CAPTURE_HELPER="${HELPER_SCRIPT}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  ./zig-out/bin/shaula capture area --json --no-preview --save --output "${history_seed_path}"
})"

printf '%s\n' "${history_seed_json}" | jq -e --arg path "${history_seed_path}" '
  .ok == true and
  .saved.ok == true and
  .path == $path
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=history_seed_invalid" >&2
  printf '%s\n' "${history_seed_json}" >&2
  exit 1
}

[[ -f "${history_seed_path}" ]] || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=history_seed_file_missing path=${history_seed_path}" >&2
  exit 1
}

open_last_exec_json="$({
  HOME=/tmp/shaula-task9-home \
  ${PLUGIN_SCRIPT} --action open-last --request-id "task9-exec-open-last"
})"
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

history_exec_json="$({
  HOME=/tmp/shaula-task9-home \
  ${PLUGIN_SCRIPT} --action history --request-id "task9-exec-history"
})"
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

mkdir -p /tmp/shaula/clipboard
printf '/tmp/shaula/task10-clipboard.png\n' > /tmp/shaula/clipboard/current-image.path
printf 'task10-png-stub' > /tmp/shaula/task10-clipboard.png

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

open_tool_dir="/tmp/shaula/task10-open-tool"
mkdir -p "${open_tool_dir}"
cat > "${open_tool_dir}/xdg-open" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exit 0
EOF
chmod +x "${open_tool_dir}/xdg-open"

open_folder_json="$({
  PATH="${open_tool_dir}:/usr/bin:/bin" \
  SHAULA_NOCTALIA_OPEN_WITH=xdg-open \
  SHAULA_NOCTALIA_OUTPUT_DIR=/tmp/shaula/task10-output \
  ${PLUGIN_SCRIPT} --action open-output-folder --request-id "task10-open-folder"
})"

printf '%s\n' "${open_folder_json}" | jq -e '
  .ok == true and
  .action.id == "open-output-folder" and
  .action.shaula_argv == ["helper","open-output-folder"] and
  .execution.mode == "execute" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.warnings | length == 0)
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_open_output_folder_contract_invalid" >&2
  printf '%s\n' "${open_folder_json}" >&2
  exit 1
}

capture_survives_json="$({
  HOME=/tmp/shaula-task9-home \
  SHAULA_RUNTIME_CAPTURE_HELPER="${HELPER_SCRIPT}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  ${PLUGIN_SCRIPT} --action capture-area --request-id "task10-capture-survives"
})"

printf '%s\n' "${capture_survives_json}" | jq -e '
  .ok == true and
  .action.id == "capture-area" and
  .execution.ok == true and
  .execution.exit_code == 0 and
  (.execution.output | fromjson | .ok == true)
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=capture_after_open_folder_invalid" >&2
  printf '%s\n' "${capture_survives_json}" >&2
  exit 1
}

open_clipboard_error_json="$({
  SHAULA_NOCTALIA_OPEN_WITH=none \
  ${PLUGIN_SCRIPT} --action open-clipboard-image --request-id "task10-open-clipboard-error"
})"

printf '%s\n' "${open_clipboard_error_json}" | jq -e '
  .ok == true and
  .action.id == "open-clipboard-image" and
  .action.shaula_argv == ["helper","open-clipboard-image"] and
  .execution.mode == "execute" and
  .execution.ok == false and
  (.execution.exit_code > 0) and
  (.warnings | index("noctalia_open_clipboard_image_tool_unavailable") != null)
' >/dev/null || {
  echo "ERR_NOCTALIA_ACTION_ASSERT_FAILED reason=execute_open_clipboard_error_contract_invalid" >&2
  printf '%s\n' "${open_clipboard_error_json}" >&2
  exit 1
}

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson action "${open_folder_json}" \
  --argjson capture "${capture_survives_json}" \
  '{
    suite: "task-10-open-folder",
    timestamp: $timestamp,
    pass: true,
    checks: {
      action_non_fatal: {
        pass: ($action.ok == true and $action.execution.ok == true and ($action.warnings | length == 0)),
        warning: null,
        exit_code: $action.execution.exit_code
      },
      capture_remains_successful: {
        pass: ($capture.ok == true and $capture.execution.ok == true),
        exit_code: $capture.execution.exit_code
      }
    }
  }' > "${OPEN_FOLDER_EVIDENCE_PATH}"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson action "${open_clipboard_error_json}" \
  --argjson capture "${capture_survives_json}" \
  '{
    suite: "task-10-open-clipboard-error",
    timestamp: $timestamp,
    pass: true,
    checks: {
      action_non_fatal: {
        pass: ($action.ok == true and $action.execution.ok == false and (($action.warnings | index("noctalia_open_clipboard_image_tool_unavailable")) != null)),
        warning: "noctalia_open_clipboard_image_tool_unavailable",
        exit_code: $action.execution.exit_code
      },
      capture_remains_successful: {
        pass: ($capture.ok == true and $capture.execution.ok == true),
        exit_code: $capture.execution.exit_code
      }
    }
  }' > "${OPEN_CLIPBOARD_ERROR_EVIDENCE_PATH}"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson menu "${menu_json}" \
  --argjson area_exec "${area_exec_json}" \
  --argjson fullscreen_exec "${fullscreen_exec_json}" \
  --argjson window_exec "${window_exec_json}" \
  --argjson open_last_exec "${open_last_exec_json}" \
  --argjson history_exec "${history_exec_json}" \
  --argjson open_folder_exec "${open_folder_json}" \
  --argjson open_clipboard_error_exec "${open_clipboard_error_json}" \
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
      },
      execute_open_output_folder_non_fatal: {
        pass: true,
        exit_code: $open_folder_exec.execution.exit_code,
        action: $open_folder_exec.action.id,
        warning: ($open_folder_exec.warnings[0] // null)
      },
      execute_open_clipboard_image_non_fatal: {
        pass: true,
        exit_code: $open_clipboard_error_exec.execution.exit_code,
        action: $open_clipboard_error_exec.action.id,
        warning: ($open_clipboard_error_exec.warnings[0] // null)
      }
    }
  }' > "${EVIDENCE_PATH}"

echo "ok noctalia_actions evidence=${EVIDENCE_PATH}"
