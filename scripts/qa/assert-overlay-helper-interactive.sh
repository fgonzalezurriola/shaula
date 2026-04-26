#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.sisyphus/evidence"
REPORT_JSON="${EVIDENCE_DIR}/task-11-interactive-overlay-report.json"
ERROR_LOG="${EVIDENCE_DIR}/task-11-interactive-overlay-error.txt"

mkdir -p "${EVIDENCE_DIR}" /tmp/shaula
: > "${ERROR_LOG}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
if ! command -v grim >/dev/null 2>&1 && [[ -z "${SHAULA_RUNTIME_CAPTURE_HELPER:-}" ]]; then
  if [[ ! -x "${helper_script}" ]]; then
    chmod +x "${helper_script}"
  fi
  export SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}"
fi

zig build >/dev/null

subchecks_json='[]'
failed_checks='[]'

SLURP_BIN_DIR="/tmp/shaula/task11-slurp-bin"
SLURP_CALL_LOG="/tmp/shaula/task11-slurp-called.log"
rm -rf "${SLURP_BIN_DIR}"
mkdir -p "${SLURP_BIN_DIR}"

# Keep fallback probe deterministic by injecting a local slurp binary.
cat > "${SLURP_BIN_DIR}/slurp" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
: "${SLURP_CALL_LOG:=/tmp/shaula/task11-slurp-called.log}"
printf 'called\n' > "${SLURP_CALL_LOG}"
printf '10,20 300x400\n'
EOF
chmod +x "${SLURP_BIN_DIR}/slurp"

run_subcheck() {
  local id="$1"
  local cmd="$2"
  local output=""
  local rc=0
  local pass=false

  set +e
  output="$(bash -lc "${cmd}" 2>&1)"
  rc=$?
  set -e

  if [[ ${rc} -eq 0 ]]; then
    pass=true
  else
    failed_checks="$(jq -c -n \
      --argjson current "${failed_checks}" \
      --arg id "${id}" \
      --arg cmd "${cmd}" \
      '$current + [{ id: $id, command: $cmd }]' \
    )"
  fi

  printf '[%s] rc=%s cmd=%s\n%s\n\n' "${id}" "${rc}" "${cmd}" "${output}" >> "${ERROR_LOG}"

  subchecks_json="$(jq -c -n \
    --argjson current "${subchecks_json}" \
    --arg id "${id}" \
    --arg cmd "${cmd}" \
    --arg output "${output}" \
    --argjson pass "${pass}" \
    '$current + [{ id: $id, pass: $pass, command: $cmd, output: $output }]' \
  )"
}

# Helper-interactive success lane: no --dry-run and forced interactive path.
run_subcheck "interactive.helper.success" "SUCCESS_PATH=/tmp/shaula/task11-interactive-success.png; rm -f \"\${SUCCESS_PATH}\"; OUT=\$(SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=interaction_drag SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output \"\${SUCCESS_PATH}\"); printf '%s\\n' \"\${OUT}\" | jq -e --arg path \"\${SUCCESS_PATH}\" '.ok==true and .mode==\"area\" and .path==\$path and .dimensions.width>0 and .dimensions.height>0' >/dev/null && [[ -f \"\${SUCCESS_PATH}\" ]]"

# Helper-interactive cancel lane must emit deterministic cancellation taxonomy.
run_subcheck "interactive.helper.cancel" "set +e; OUT=\$(SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=interaction_cancel SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json 2>&1); RC=\$?; set -e; [[ \${RC} -ne 0 ]] && printf '%s\\n' \"\${OUT}\" | jq -e '.ok==false and .error.code==\"ERR_SELECTION_CANCELLED\"' >/dev/null"

# Helper-interactive malformed lane must map to protocol-invalid deterministically.
run_subcheck "interactive.helper.error" "set +e; OUT=\$(SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE=malformed SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json 2>&1); RC=\$?; set -e; [[ \${RC} -ne 0 ]] && printf '%s\\n' \"\${OUT}\" | jq -e '.ok==false and .error.code==\"ERR_OVERLAY_PROTOCOL_INVALID\"' >/dev/null"

# Fallback lane: force helper process unavailable, then prove slurp fallback was used.
run_subcheck "interactive.helper.fallback" "FALLBACK_PATH=/tmp/shaula/task11-interactive-fallback.png; rm -f \"\${FALLBACK_PATH}\" \"${SLURP_CALL_LOG}\"; OUT=\$(PATH=\"${SLURP_BIN_DIR}:\${PATH}\" SLURP_CALL_LOG=\"${SLURP_CALL_LOG}\" SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=0 SHAULA_OVERLAY_HELPER_BIN=/definitely/missing/shaula-overlay SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output \"\${FALLBACK_PATH}\"); printf '%s\\n' \"\${OUT}\" | jq -e --arg path \"\${FALLBACK_PATH}\" '.ok==true and .mode==\"area\" and .path==\$path and .dimensions.width>0 and .dimensions.height>0' >/dev/null && [[ -f \"\${FALLBACK_PATH}\" ]] && [[ -f \"${SLURP_CALL_LOG}\" ]]"

suite_pass="$(jq -e 'all(.[]; .pass == true)' <<<"${subchecks_json}" >/dev/null && echo true || echo false)"
timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

jq -n \
  --arg timestamp "${timestamp}" \
  --argjson pass "${suite_pass}" \
  --argjson subchecks "${subchecks_json}" \
  '{
    suite: "task-11-interactive-overlay",
    timestamp: $timestamp,
    pass: $pass,
    script: "scripts/qa/assert-overlay-helper-interactive.sh",
    subchecks: $subchecks
  }' > "${REPORT_JSON}"

if [[ "${suite_pass}" != "true" ]]; then
  echo "QA failure summary (interactive overlay):" >&2
  jq -r '.[] | "- \(.id): \(.command)"' <<<"${failed_checks}" >&2
  echo "ERR_QA_INTERACTIVE_OVERLAY_FAILED report=${REPORT_JSON} log=${ERROR_LOG}" >&2
  exit 1
fi

echo "ok qa_interactive_overlay_helper_lanes report=${REPORT_JSON}"
