#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ADAPTER_SCRIPT="${ROOT_DIR}/integrations/noctalia/noctalia-action-adapter.sh"

REQUEST_ID="noctalia-req-001"
MODE="menu"
ACTION_ID=""
DRY_RUN=0
SHAULA_BIN="${SHAULA_BIN:-}"

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
    --dry-run)
      DRY_RUN=1
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
    --json)
      shift
      ;;
    # Backward-compatible accepted flags from the early PoC; intentionally unused.
    --socket)
      shift 2
      ;;
    --timeout-ms)
      shift 2
      ;;
    *)
      echo "ERR_NOCTALIA_PLUGIN_USAGE reason=unknown_flag flag=$1" >&2
      exit 1
      ;;
  esac
done

if [[ "${MODE}" == "action" && -z "${ACTION_ID}" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_USAGE reason=missing_action_id" >&2
  exit 1
fi

if [[ ! -f "${ADAPTER_SCRIPT}" ]]; then
  echo "ERR_NOCTALIA_PLUGIN_ADAPTER_MISSING path=integrations/noctalia/noctalia-action-adapter.sh" >&2
  exit 1
fi

adapter_cmd=(bash "${ADAPTER_SCRIPT}" --request-id "${REQUEST_ID}")
if [[ -n "${SHAULA_BIN}" ]]; then
  adapter_cmd+=(--shaula-bin "${SHAULA_BIN}")
fi

if [[ "${MODE}" == "menu" ]]; then
  adapter_cmd+=(--menu)
else
  adapter_cmd+=(--action "${ACTION_ID}")
  if [[ ${DRY_RUN} -eq 1 ]]; then
    adapter_cmd+=(--dry-run)
  else
    adapter_cmd+=(--execute)
  fi
fi

"${adapter_cmd[@]}"
