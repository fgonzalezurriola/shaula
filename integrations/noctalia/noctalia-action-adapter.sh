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

if [[ "${MODE}" == "action" && ${EXECUTE} -eq 1 && ! -x "${SHAULA_BIN}" ]]; then
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
    capture-area) printf 'Capture Area' ;;
    capture-fullscreen) printf 'Capture Fullscreen' ;;
    capture-window) printf 'Capture Window' ;;
    open-last) printf 'Open Last' ;;
    history) printf 'History' ;;
    *) return 1 ;;
  esac
}

action_command_json() {
  case "$1" in
    capture-area) printf '["capture","area","--json"]' ;;
    capture-fullscreen) printf '["capture","fullscreen","--json"]' ;;
    capture-window) printf '["capture","window","--json"]' ;;
    open-last) printf '["history","show","--json","--id","latest"]' ;;
    history) printf '["history","list","--json"]' ;;
    *) return 1 ;;
  esac
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
  a3="$(build_action_entry capture-window)"
  a4="$(build_action_entry open-last)"
  a5="$(build_action_entry history)"

  printf '{"ok":true,"plugin":"noctalia","optional":true,"request_id":"%s","menu":{"minimal":true,"actions":[%s,%s,%s,%s,%s]},"warnings":[]}\n' \
    "${REQUEST_ID}" \
    "${a1}" "${a2}" "${a3}" "${a4}" "${a5}"
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

readarray -t argv < <(python3 - "${ACTION_ID}" <<'PY'
import json
import sys

action = sys.argv[1]
mapping = {
    "capture-area": ["capture", "area", "--json"],
    "capture-fullscreen": ["capture", "fullscreen", "--json"],
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
