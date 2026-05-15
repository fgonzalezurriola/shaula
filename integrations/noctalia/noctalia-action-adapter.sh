#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

REQUEST_ID="noctalia-req-001"
MODE="menu"
ACTION_ID=""
EXECUTE=0
SHAULA_BIN="${SHAULA_BIN:-${ROOT_DIR}/zig-out/bin/shaula}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --menu)
      MODE="menu"
      shift
      ;;
    --action)
      MODE="action"
      ACTION_ID="$2"
      shift 2
      ;;
    --execute)
      EXECUTE=1
      shift
      ;;
    --dry-run)
      EXECUTE=0
      shift
      ;;
    --request-id)
      REQUEST_ID="$2"
      shift 2
      ;;
    --shaula-bin)
      SHAULA_BIN="$2"
      shift 2
      ;;
    *)
      echo "ERR_NOCTALIA_ADAPTER_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

if [[ "${MODE}" == "action" && -z "${ACTION_ID}" ]]; then
  echo "ERR_NOCTALIA_ADAPTER_USAGE reason=missing_action_id" >&2
  exit 1
fi

if [[ "${MODE}" == "action" && ${EXECUTE} -eq 1 && "${ACTION_ID}" != "open-clipboard-image" && ! -x "${SHAULA_BIN}" ]]; then
  echo "ERR_NOCTALIA_ADAPTER_EXECUTION_UNAVAILABLE reason=shaula_binary_missing path=${SHAULA_BIN}" >&2
  exit 1
fi

json_escape() {
  python3 - "$1" <<'PY'
import json
import sys
print(json.dumps(sys.argv[1], separators=(",", ":")))
PY
}

action_label() {
 case "$1" in
 capture-quick) printf 'Quick Capture' ;;
 capture-area) printf 'Quick Capture' ;;
 capture-fullscreen) printf 'Capture Fullscreen' ;;
 capture-all-screens) printf 'Capture All Screens' ;;
 capture-window) printf 'Capture Window' ;;
 open-last) printf 'Open Last' ;;
 history) printf 'History' ;;
 open-output-folder) printf 'Open Output Folder' ;;
 open-clipboard-image) printf 'Open Clipboard Image' ;;
 *) return 1 ;;
 esac
}

action_command_json() {
 case "$1" in
 capture-quick) printf '["capture","quick","--json"]' ;;
 capture-area) printf '["capture","area","--json"]' ;;
 capture-fullscreen) printf '["capture","fullscreen","--json","--copy"]' ;;
 capture-all-screens) printf '["capture","all-screens","--json","--copy"]' ;;
 capture-window) printf '["capture","window","--json"]' ;;
 open-last) printf '["history","show","--json","--id","latest"]' ;;
 history) printf '["history","list","--json"]' ;;
 open-output-folder) printf '["directory","screenshots","--open","--json"]' ;;
 open-clipboard-image) printf '["helper","open-clipboard-image"]' ;;
 *) return 1 ;;
 esac
}

warning_array_json() {
  local warning_token="$1"
  if [[ -z "${warning_token}" ]]; then
    printf '[]'
    return
  fi

  local warning_json
  warning_json="$(json_escape "${warning_token}")"
  printf '[%s]' "${warning_json}"
}

OPEN_ACTION_RC=0
OPEN_ACTION_OUTPUT=""

# Executes opener command in a compositor-safe way.
# Contract: failures are reported via exit_code/warnings in JSON and remain non-fatal.
execute_open_target() {
  local target="$1"
  local opener_hint="${SHAULA_NOCTALIA_OPEN_WITH:-auto}"
  local opener=""
  local opener_subcommand=""

  case "${opener_hint}" in
    xdg-open)
      opener="xdg-open"
      ;;
    gio)
      opener="gio"
      opener_subcommand="open"
      ;;
    none)
      OPEN_ACTION_OUTPUT="ERR_NOCTALIA_ACTION_OPEN_TOOL_UNAVAILABLE tool=xdg-open fallback=gio"
      OPEN_ACTION_RC=127
      return
      ;;
    auto)
      if command -v xdg-open >/dev/null 2>&1; then
        opener="xdg-open"
      elif command -v gio >/dev/null 2>&1; then
        opener="gio"
        opener_subcommand="open"
      fi
      ;;
    *)
      OPEN_ACTION_OUTPUT="ERR_NOCTALIA_ACTION_OPEN_TOOL_UNAVAILABLE reason=invalid_hint hint=${opener_hint}"
      OPEN_ACTION_RC=127
      return
      ;;
  esac

  if [[ -z "${opener}" ]]; then
    OPEN_ACTION_OUTPUT="ERR_NOCTALIA_ACTION_OPEN_TOOL_UNAVAILABLE tool=xdg-open fallback=gio"
    OPEN_ACTION_RC=127
    return
  fi

  set +e
  if [[ "${opener}" == "gio" ]]; then
    OPEN_ACTION_OUTPUT="$(${opener} ${opener_subcommand} "${target}" 2>&1)"
  else
    OPEN_ACTION_OUTPUT="$(${opener} "${target}" 2>&1)"
  fi
  OPEN_ACTION_RC=$?
  set -e
}

resolve_clipboard_image_path() {
  local state_file="${SHAULA_CLIPBOARD_STATE_FILE:-/tmp/shaula/clipboard/current-image.path}"

  if [[ ! -f "${state_file}" ]]; then
    return 1
  fi

  local raw_path
  raw_path="$(python3 - "${state_file}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
try:
    content = path.read_text(encoding="utf-8").strip()
except Exception:
    content = ""
print(content)
PY
)"

  if [[ -z "${raw_path}" ]]; then
    return 1
  fi

  printf '%s' "${raw_path}"
}

build_action_entry() {
  local id="$1"
  local label
  label="$(action_label "${id}")" || return 1
  local cmd
  cmd="$(action_command_json "${id}")" || return 1
  local label_json
  label_json="$(json_escape "${label}")"
  printf '{"id":"%s","label":%s,"shaula_argv":%s}' "${id}" "${label_json}" "${cmd}"
}

if [[ "${MODE}" == "menu" ]]; then
 a1="$(build_action_entry capture-area)"
 a2="$(build_action_entry capture-fullscreen)"
 a3="$(build_action_entry capture-all-screens)"
 a4="$(build_action_entry capture-window)"
 a5="$(build_action_entry open-last)"
 a6="$(build_action_entry history)"
 a7="$(build_action_entry open-output-folder)"
 a8="$(build_action_entry open-clipboard-image)"

 printf '{"ok":true,"plugin":"noctalia","optional":true,"request_id":"%s","menu":{"minimal":true,"actions":[%s,%s,%s,%s,%s,%s,%s,%s]},"warnings":[]}\n' \
 "${REQUEST_ID}" \
 "${a1}" "${a2}" "${a3}" "${a4}" "${a5}" "${a6}" "${a7}" "${a8}"
  exit 0
fi

label="$(action_label "${ACTION_ID}")" || {
  echo "ERR_NOCTALIA_ACTION_UNKNOWN action=${ACTION_ID}" >&2
  exit 1
}

command_json="$(action_command_json "${ACTION_ID}")" || {
  echo "ERR_NOCTALIA_ACTION_UNKNOWN action=${ACTION_ID}" >&2
  exit 1
}

if [[ ${EXECUTE} -eq 0 ]]; then
  label_json="$(json_escape "${label}")"
  printf '{"ok":true,"plugin":"noctalia","optional":true,"request_id":"%s","action":{"id":"%s","label":%s,"shaula_argv":%s},"execution":{"mode":"dry-run"},"warnings":[]}\n' \
    "${REQUEST_ID}" \
    "${ACTION_ID}" \
    "${label_json}" \
    "${command_json}"
  exit 0
fi

if [[ "${ACTION_ID}" == "open-output-folder" || "${ACTION_ID}" == "open-clipboard-image" ]]; then
  warning_token=""

  if [[ "${ACTION_ID}" == "open-output-folder" ]]; then
    OPEN_ACTION_OUTPUT="$(${SHAULA_BIN} directory screenshots --open --json 2>&1)" || OPEN_ACTION_RC=$?
    [[ ${OPEN_ACTION_RC} -ne 0 ]] && warning_token="noctalia_open_output_folder_tool_unavailable"
  else
    clipboard_path="$(resolve_clipboard_image_path || true)"
    if [[ -z "${clipboard_path}" || ! -f "${clipboard_path}" ]]; then
      OPEN_ACTION_OUTPUT="ERR_NOCTALIA_ACTION_CLIPBOARD_IMAGE_MISSING path=/tmp/shaula/clipboard/current-image.path"
      OPEN_ACTION_RC=66
      warning_token="noctalia_open_clipboard_image_missing"
    else
      execute_open_target "${clipboard_path}"
      if [[ ${OPEN_ACTION_RC} -ne 0 ]]; then
        warning_token="noctalia_open_clipboard_image_tool_unavailable"
      fi
    fi
  fi

  execution_ok="false"
  if [[ ${OPEN_ACTION_RC} -eq 0 ]]; then
    execution_ok="true"
  fi

  label_json="$(json_escape "${label}")"
  command_output_json="$(json_escape "${OPEN_ACTION_OUTPUT}")"
  warnings_json="$(warning_array_json "${warning_token}")"

  printf '{"ok":true,"plugin":"noctalia","optional":true,"request_id":"%s","action":{"id":"%s","label":%s,"shaula_argv":%s},"execution":{"mode":"execute","ok":%s,"exit_code":%d,"output":%s},"warnings":%s}\n' \
    "${REQUEST_ID}" \
    "${ACTION_ID}" \
    "${label_json}" \
    "${command_json}" \
    "${execution_ok}" \
    "${OPEN_ACTION_RC}" \
    "${command_output_json}" \
    "${warnings_json}"
  exit 0
fi

readarray -t argv < <(python3 - "${ACTION_ID}" <<'PY'
import json
import sys

action = sys.argv[1]
mapping = {
    "capture-quick": ["capture", "quick", "--json"],
    "capture-area": ["capture", "area", "--json"],
    "capture-fullscreen": ["capture", "fullscreen", "--json", "--copy"],
    "capture-all-screens": ["capture", "all-screens", "--json", "--copy"],
    "capture-window": ["capture", "window", "--json"],
    "open-last": ["history", "show", "--json", "--id", "latest"],
    "history": ["history", "list", "--json"],
}

for arg in mapping[action]:
    print(arg)
PY
)

set +e
command_output="$(${SHAULA_BIN} "${argv[@]}" 2>&1)"
command_rc=$?
set -e

label_json="$(json_escape "${label}")"
command_output_json="$(json_escape "${command_output}")"
execution_ok="false"
if [[ ${command_rc} -eq 0 ]]; then
  execution_ok="true"
fi

printf '{"ok":%s,"plugin":"noctalia","optional":true,"request_id":"%s","action":{"id":"%s","label":%s,"shaula_argv":%s},"execution":{"mode":"execute","ok":%s,"exit_code":%d,"output":%s},"warnings":[]}\n' \
  "${execution_ok}" \
  "${REQUEST_ID}" \
  "${ACTION_ID}" \
  "${label_json}" \
  "${command_json}" \
  "${execution_ok}" \
  "${command_rc}" \
  "${command_output_json}"

if [[ ${command_rc} -ne 0 ]]; then
  exit 1
fi
