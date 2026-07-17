#!/usr/bin/env bash
set -euo pipefail

SHAULA_BIN="${1:?shaula binary is required}"
FAKE_CAPTURE_HELPER="${2:?fake capture helper is required}"
TMP_DIR="$(mktemp -d)"
HISTORY_PATH="/tmp/shaula/history/latest.v1"
HISTORY_BACKUP="${TMP_DIR}/history.backup"
HISTORY_EXISTED=0
CLIPBOARD_STATE="/tmp/shaula/clipboard/current-image.path"
CLIPBOARD_BACKUP="${TMP_DIR}/clipboard-state.backup"
CLIPBOARD_EXISTED=0

cleanup() {
  if [[ ${HISTORY_EXISTED} -eq 1 ]]; then
    mkdir -p "$(dirname "${HISTORY_PATH}")"
    cp "${HISTORY_BACKUP}" "${HISTORY_PATH}"
  else
    rm -f "${HISTORY_PATH}"
  fi
  if [[ ${CLIPBOARD_EXISTED} -eq 1 ]]; then
    mkdir -p "$(dirname "${CLIPBOARD_STATE}")"
    cp "${CLIPBOARD_BACKUP}" "${CLIPBOARD_STATE}"
  else
    rm -f "${CLIPBOARD_STATE}"
  fi
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

if [[ -f "${HISTORY_PATH}" ]]; then
  HISTORY_EXISTED=1
  cp "${HISTORY_PATH}" "${HISTORY_BACKUP}"
fi
if [[ -f "${CLIPBOARD_STATE}" ]]; then
  CLIPBOARD_EXISTED=1
  cp "${CLIPBOARD_STATE}" "${CLIPBOARD_BACKUP}"
fi

mkdir -p "${TMP_DIR}/bin" /tmp/shaula/history
cat > "${TMP_DIR}/bin/niri" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "${3:-}" in
  outputs)
    printf '%s\n' '{"DP-1":{"name":"DP-1","focused":true,"logical":{"x":0,"y":0,"width":1920,"height":1080},"scale":1.0}}'
    ;;
  workspaces)
    printf '%s\n' '[{"id":7,"name":"main","output":"DP-1","is_focused":true,"is_active":true}]'
    ;;
  windows)
    printf '%s\n' '[{"id":9,"app_id":"dev.shaula.test","title":"Test","workspace_id":7,"is_focused":true}]'
    ;;
  *)
    exit 1
    ;;
esac
EOF
cat > "${TMP_DIR}/bin/wl-copy" <<'EOF'
#!/usr/bin/env bash
cat >/dev/null
EOF
cat > "${TMP_DIR}/bin/notify-send" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "${TMP_DIR}/bin/niri" "${TMP_DIR}/bin/wl-copy" \
  "${TMP_DIR}/bin/notify-send"

PATH="${TMP_DIR}/bin:${PATH}"
export PATH

capture_path="${TMP_DIR}/noninteractive.png"
capture_json="$(
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_REGION_CAPTURE_MODE=live \
  SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1 \
  SHAULA_OVERLAY_HELPER_BIN=/definitely/missing/shaula-overlay \
  SHAULA_RUNTIME_CAPTURE_HELPER="${FAKE_CAPTURE_HELPER}" \
  "${SHAULA_BIN}" capture area --json --no-preview --output "${capture_path}"
)"
printf '%s\n' "${capture_json}" | jq -e --arg path "${capture_path}" '
  .ok == true and .result.mode == "area" and .result.path == $path
' >/dev/null
[[ -s "${capture_path}" ]]

set +e
clipboard_error="$(SHAULA_CLIPBOARD_AVAILABLE=0 "${SHAULA_BIN}" clipboard import-image --json)"
clipboard_rc=$?
set -e
[[ ${clipboard_rc} -ne 0 ]]
printf '%s\n' "${clipboard_error}" | jq -e '
  .ok == false and .error.code == "ERR_CLIPBOARD_UNAVAILABLE"
' >/dev/null

"${SHAULA_BIN}" clipboard copy-image --json --input "${capture_path}" >/dev/null
import_path="${TMP_DIR}/imported.png"
import_json="$("${SHAULA_BIN}" clipboard import-image --json --output "${import_path}")"
printf '%s\n' "${import_json}" | jq -e --arg path "${import_path}" '
  .ok == true and .result.path == $path
' >/dev/null
cmp "${capture_path}" "${import_path}"

printf '%s|image/png|640|360|niri-wayland-direct|2026-07-11T20:00:00Z\n' \
  "${capture_path}" > "${HISTORY_PATH}"
history_json="$("${SHAULA_BIN}" history show --json --id latest)"
printf '%s\n' "${history_json}" | jq -e --arg path "${capture_path}" '
  .ok == true and .command == "history show" and
  .result.id == "latest" and .result.entry.path == $path
' >/dev/null

config_path="${TMP_DIR}/config.toml"
cat > "${config_path}" <<'EOF'
# preserve this comment
[preview.window]
mode = "floating" # preserve inline comment
focused = true
close_preview_on_save = true
width = 1234
height = 777
default_column_display = "normal"
EOF
SHAULA_CONFIG_FILE="${config_path}" \
  "${SHAULA_BIN}" config save --json --height 778 >/dev/null
grep -Fxq '# preserve this comment' "${config_path}"
grep -Fxq 'mode = "floating" # preserve inline comment' "${config_path}"
grep -Fxq 'width = 1234' "${config_path}"
grep -Fxq 'height = 778' "${config_path}"

explore_json="$(
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_OVERLAY_OUTPUT_NAME=DP-1 \
  "${SHAULA_BIN}" explore --json
)"
printf '%s\n' "${explore_json}" | jq -e '
  .ok == true and .warnings == [] and
  .result.focused.output_id == "DP-1" and
  .result.focused.workspace_id == 7 and
  .result.focused.window_id == 9 and
  (.result.outputs | length) == 1 and
  (.result.workspaces | length) == 1 and
  (.result.windows | length) == 1 and
  .result.recommended_capture.mode == "output"
' >/dev/null

notify_json="$("${SHAULA_BIN}" notify test --kind saved)"
printf '%s\n' "${notify_json}" | jq -e '
  .ok == true and .command == "notify test" and
  .result.kind == "saved" and .result.delivered == true
' >/dev/null

set +e
overlay_json="$(
  SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=malformed \
  "${SHAULA_BIN}" capture area --dry-run --json
)"
overlay_rc=$?
set -e
[[ ${overlay_rc} -ne 0 ]]
printf '%s\n' "${overlay_json}" | jq -e '
  .ok == false and .error.code == "ERR_OVERLAY_PROTOCOL_INVALID" and
  .error.details.mode == "area"
' >/dev/null

echo "ok command_compatibility"
