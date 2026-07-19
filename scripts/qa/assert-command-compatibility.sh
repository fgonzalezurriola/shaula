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
cat > "${TMP_DIR}/bin/shaula-clipboard-provider" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cat >/dev/null
if [[ ${SHAULA_TEST_CLIPBOARD_MODE:-ready} == failure ]]; then
  exit 47
fi
printf 'READY shaula-clipboard/1\n'
sleep 1
EOF
cat > "${TMP_DIR}/bin/notify-send" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
cat > "${TMP_DIR}/bin/portal-capture-helper" <<EOF
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "\$*" > "${TMP_DIR}/portal-argv.log"
if [[ -n "\${SHAULA_FAKE_PORTAL_EXIT_CODE:-}" ]]; then
  exit "\${SHAULA_FAKE_PORTAL_EXIT_CODE}"
fi
exec "${FAKE_CAPTURE_HELPER}" "\$@"
EOF
cat > "${TMP_DIR}/bin/forbidden-overlay" <<EOF
#!/usr/bin/env bash
touch "${TMP_DIR}/overlay-launched"
exit 1
EOF
chmod +x "${TMP_DIR}/bin/niri" \
  "${TMP_DIR}/bin/shaula-clipboard-provider" \
  "${TMP_DIR}/bin/notify-send" \
  "${TMP_DIR}/bin/portal-capture-helper" \
  "${TMP_DIR}/bin/forbidden-overlay"

PATH="${TMP_DIR}/bin:${PATH}"
export PATH
export SHAULA_CLIPBOARD_PROVIDER_BIN="${TMP_DIR}/bin/shaula-clipboard-provider"

capture_path="${TMP_DIR}/noninteractive.png"
capture_json="$(
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_REGION_CAPTURE_MODE=live \
  SHAULA_GRIM_AVAILABLE=1 \
  SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1 \
  SHAULA_OVERLAY_HELPER_BIN=/definitely/missing/shaula-overlay \
  SHAULA_RUNTIME_CAPTURE_HELPER="${FAKE_CAPTURE_HELPER}" \
  "${SHAULA_BIN}" capture area --json --no-preview --output "${capture_path}"
)"
printf '%s\n' "${capture_json}" | jq -e --arg path "${capture_path}" '
  .ok == true and .result.mode == "area" and .result.path == $path
' >/dev/null
[[ -s "${capture_path}" ]]

portal_path="${TMP_DIR}/portal-area.png"
portal_json="$(
  SHAULA_COMPOSITOR=gnome \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=0 \
  SHAULA_PORTAL_AVAILABLE=1 \
  SHAULA_REGION_CAPTURE_MODE=frozen \
  SHAULA_OVERLAY_HELPER_BIN="${TMP_DIR}/bin/forbidden-overlay" \
  SHAULA_RUNTIME_CAPTURE_HELPER="${TMP_DIR}/bin/portal-capture-helper" \
  "${SHAULA_BIN}" capture area --json --no-preview --output "${portal_path}"
)"
printf '%s\n' "${portal_json}" | jq -e --arg path "${portal_path}" '
  .ok == true and .result.path == $path and
  .result.backend_used == "portal-screenshot"
' >/dev/null
[[ -s "${portal_path}" ]]
[[ ! -e "${TMP_DIR}/overlay-launched" ]]
grep -q -- '--backend portal-screenshot --mode area' "${TMP_DIR}/portal-argv.log"
! grep -q -- '--geometry' "${TMP_DIR}/portal-argv.log"

set +e
portal_cancel_json="$(
  SHAULA_COMPOSITOR=gnome \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=0 \
  SHAULA_PORTAL_AVAILABLE=1 \
  SHAULA_FAKE_PORTAL_EXIT_CODE=33 \
  SHAULA_OVERLAY_HELPER_BIN="${TMP_DIR}/bin/forbidden-overlay" \
  SHAULA_RUNTIME_CAPTURE_HELPER="${TMP_DIR}/bin/portal-capture-helper" \
  "${SHAULA_BIN}" capture area --json --no-preview \
    --output "${TMP_DIR}/portal-cancel.png"
)"
portal_cancel_rc=$?
set -e
[[ ${portal_cancel_rc} -ne 0 ]]
printf '%s\n' "${portal_cancel_json}" | jq -e '
  .ok == false and .error.code == "ERR_SELECTION_CANCELLED"
' >/dev/null
[[ ! -e "${TMP_DIR}/overlay-launched" ]]

set +e
clipboard_error="$(SHAULA_CLIPBOARD_AVAILABLE=0 "${SHAULA_BIN}" clipboard copy-image --json --input "${capture_path}")"
clipboard_rc=$?
set -e
[[ ${clipboard_rc} -ne 0 ]]
printf '%s\n' "${clipboard_error}" | jq -e '
  .ok == false and .error.code == "ERR_CLIPBOARD_UNAVAILABLE"
' >/dev/null

set +e
clipboard_text_error="$(
  SHAULA_TEST_CLIPBOARD_MODE=failure \
  "${SHAULA_BIN}" clipboard copy-text --json --text 'payload'
)"
clipboard_text_rc=$?
set -e
[[ ${clipboard_text_rc} -ne 0 ]]
printf '%s\n' "${clipboard_text_error}" | jq -e '
  .ok == false and .error.code == "ERR_CLIPBOARD_COPY_FAILED" and
  .error.message == "clipboard text copy failed" and
  .error.details.provider_status == "provider_failed"
' >/dev/null
! grep -Fq 'clipboard image copy failed' <<<"${clipboard_text_error}"

"${SHAULA_BIN}" clipboard copy-image --json --input "${capture_path}" >/dev/null
import_path="${TMP_DIR}/imported.png"
import_json="$("${SHAULA_BIN}" clipboard import-image --json --output "${import_path}")"
printf '%s\n' "${import_json}" | jq -e --arg path "${import_path}" '
  .ok == true and .result.path == $path
' >/dev/null
cmp "${capture_path}" "${import_path}"

printf '%s|image/png|640|360|grim-wlroots|2026-07-11T20:00:00Z\n' \
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
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=1 \
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
