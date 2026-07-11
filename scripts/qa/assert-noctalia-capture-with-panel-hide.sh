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

./dev build >/dev/null

mkdir -p "${ROOT_DIR}/.qa/evidence" /tmp/shaula

token_file="/tmp/shaula/task8-noctalia-panel-hidden.token"
capture_path="/tmp/shaula/task8-noctalia-panel-hide.png"
timeout_evidence="${ROOT_DIR}/.qa/evidence/task-8-shell-artifact-guard-error.txt"

rm -f "${token_file}" "${capture_path}" "${timeout_evidence}"

# Simulate panel hide by creating handshake token shortly after trigger.
python3 - "${token_file}" <<'PY' &
import pathlib
import sys
import time

path = pathlib.Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
path.unlink(missing_ok=True)
time.sleep(0.015)
path.write_text("hidden", encoding="utf-8")
PY
token_writer_pid=$!

handshake_json="$({
  SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_PANEL_HIDDEN_TOKEN_FILE="${token_file}" \
  SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS=220 \
  SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS=120 \
  SHAULA_CAPTURE_SETTLE_BARRIER_MS=30 \
  SHAULA_CAPTURE_INJECT_PANEL_MARKER=1 \
  SHAULA_PANEL_HIDDEN=0 \
  ./build/shaula capture area --json --no-preview --output "${capture_path}"
} )"

wait "${token_writer_pid}"

printf '%s\n' "${handshake_json}" | jq -e --arg path "${capture_path}" '
  .ok == true and
  .mode == "area" and
  .path == $path and
  (.warnings | index("capture_precondition_panel_hidden_handshake") != null)
' >/dev/null || {
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=handshake_capture_contract" >&2
  printf '%s\n' "${handshake_json}" >&2
  exit 1
}

[[ -f "${capture_path}" ]] || {
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=handshake_capture_file_missing" >&2
  exit 1
}

fallback_path="/tmp/shaula/task8-noctalia-fallback.png"
rm -f "${fallback_path}"
fallback_json="$({
  SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS=260 \
  SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS=0 \
  SHAULA_CAPTURE_SETTLE_BARRIER_MS=35 \
  SHAULA_PANEL_HIDDEN_TOKEN_FILE=/tmp/shaula/nonexistent-task8-token \
  SHAULA_CAPTURE_INJECT_PANEL_MARKER=1 \
  SHAULA_PANEL_MARKER_VISIBLE_UNTIL_MS="$(( $(date +%s%3N) + 10 ))" \
  SHAULA_PANEL_HIDDEN=0 \
  ./build/shaula capture area --json --no-preview --output "${fallback_path}"
} )"

printf '%s\n' "${fallback_json}" | jq -e --arg path "${fallback_path}" '
  .ok == true and
  .mode == "area" and
  .path == $path and
  (.warnings | index("capture_precondition_settle_barrier") != null)
' >/dev/null || {
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=fallback_capture_contract" >&2
  printf '%s\n' "${fallback_json}" >&2
  exit 1
}

[[ -f "${fallback_path}" ]] || {
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=fallback_capture_file_missing" >&2
  exit 1
}

timeout_path="/tmp/shaula/task8-noctalia-timeout.png"
rm -f "${timeout_path}"

set +e
timeout_json="$({
  SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" \
  SHAULA_COMPOSITOR=niri \
  NIRI_SOCKET=/tmp/niri.sock \
  WAYLAND_DISPLAY=wayland-1 \
  SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS=15 \
  SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE=1 \
  SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS=0 \
  SHAULA_CAPTURE_SETTLE_BARRIER_MS=90 \
  SHAULA_PANEL_HIDDEN_TOKEN_FILE=/tmp/shaula/nonexistent-timeout-token \
  SHAULA_CAPTURE_INJECT_PANEL_MARKER=1 \
  SHAULA_PANEL_HIDDEN=0 \
  ./build/shaula capture area --json --output "${timeout_path}" 2>&1
} )"
timeout_rc=$?
set -e

printf '%s\n' "${timeout_json}" > "${timeout_evidence}"

if [[ ${timeout_rc} -eq 0 ]]; then
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=timeout_path_unexpected_success" >&2
  printf '%s\n' "${timeout_json}" >&2
  exit 1
fi

printf '%s\n' "${timeout_json}" | jq -e '
  .ok == false and
  .error.code == "ERR_CAPTURE_PRECONDITION_TIMEOUT" and
  .error.retryable == true and
  (.warnings | index("capture_precondition_guard_timeout") != null)
' >/dev/null || {
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=timeout_error_contract" >&2
  printf '%s\n' "${timeout_json}" >&2
  exit 1
}

if [[ -e "${timeout_path}" ]]; then
  echo "ERR_NOCTALIA_PANEL_HIDE_INVALID reason=timeout_generated_output" >&2
  exit 1
fi

echo "ok noctalia_capture_with_panel_hide timeout_error=ERR_CAPTURE_PRECONDITION_TIMEOUT"
