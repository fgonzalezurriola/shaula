#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

TOPN=20
SHOTS=25

while [[ $# -gt 0 ]]; do
  case "$1" in
    --topn)
      TOPN="${2:-}"
      shift 2
      ;;
    --shots)
      SHOTS="${2:-}"
      shift 2
      ;;
    *)
      echo "ERR_HISTORY_TOPN_VIOLATION reason=unsupported_arg arg=$1" >&2
      exit 1
      ;;
  esac
done

if ! [[ "${TOPN}" =~ ^[0-9]+$ ]] || ! [[ "${SHOTS}" =~ ^[0-9]+$ ]]; then
  echo "ERR_HISTORY_TOPN_VIOLATION reason=invalid_numeric_args" >&2
  exit 1
fi

if [[ "${TOPN}" -le 0 ]] || [[ "${SHOTS}" -le 0 ]]; then
  echo "ERR_HISTORY_TOPN_VIOLATION reason=non_positive_args" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

helper_script="${ROOT_DIR}/scripts/qa/fake_runtime_capture_helper.py"
if [[ ! -x "${helper_script}" ]]; then
  chmod +x "${helper_script}"
fi

zig build >/dev/null

history_file="/tmp/shaula/history/latest.v1"
rm -f "${history_file}"

capture_dir="/tmp/shaula/task6-history-topn"
rm -rf "${capture_dir}"
mkdir -p "${capture_dir}"

for ((i = 1; i <= SHOTS; i++)); do
  capture_path="${capture_dir}/capture-$(printf '%03d' "${i}").png"
  capture_json="$(SHAULA_RUNTIME_CAPTURE_HELPER="${helper_script}" SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --save --output "${capture_path}")"

  printf '%s\n' "${capture_json}" | jq -e --arg p "${capture_path}" '
    .ok == true and
    .saved.ok == true and
    .saved.path == $p and
    .path == $p
  ' >/dev/null || {
    echo "ERR_HISTORY_TOPN_VIOLATION reason=capture_contract shot=${i}" >&2
    printf '%s\n' "${capture_json}" >&2
    exit 1
  }
done

history_json="$(./zig-out/bin/shaula history list --json)"

expected_len="${TOPN}"
if [[ "${SHOTS}" -lt "${TOPN}" ]]; then
  expected_len="${SHOTS}"
fi

printf '%s\n' "${history_json}" | jq -e --argjson expected_len "${expected_len}" '
  .ok == true and
  .command == "history list" and
  (.result.entries | type == "array") and
  (.result.entries | length == $expected_len) and
  (all(.result.entries[];
    (.path | type == "string" and length > 0) and
    (.mime | type == "string" and length > 0) and
    (.backend_used | type == "string" and length > 0) and
    (.timestamp | type == "string" and length > 0) and
    (.dimensions.width | type == "number" and . > 0) and
    (.dimensions.height | type == "number" and . > 0)
  ))
' >/dev/null || {
  echo "ERR_HISTORY_TOPN_VIOLATION reason=history_contract" >&2
  printf '%s\n' "${history_json}" >&2
  exit 1
}

expected_paths="$(
  for ((i = SHOTS; i > SHOTS - expected_len; i--)); do
    printf '"%s/capture-%03d.png"\n' "${capture_dir}" "${i}"
  done | jq -s '.'
)"

printf '%s\n' "${history_json}" | jq -e --argjson expected "${expected_paths}" '
  (.result.entries | map(.path)) == $expected
' >/dev/null || {
  echo "ERR_HISTORY_TOPN_VIOLATION reason=ordering_or_trimming" >&2
  printf '%s\n' "${history_json}" >&2
  exit 1
}

evidence_dir="${ROOT_DIR}/.sisyphus/evidence"
mkdir -p "${evidence_dir}"
evidence_file="${evidence_dir}/task-6-history-topn.txt"

{
  echo "topn=${TOPN}"
  echo "shots=${SHOTS}"
  echo "expected_len=${expected_len}"
  echo "ordered_paths=true"
  printf '%s\n' "${history_json}"
} > "${evidence_file}"

echo "ok history_topn deterministic topn=${TOPN} shots=${SHOTS}"
