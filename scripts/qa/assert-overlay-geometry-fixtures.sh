#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

EVIDENCE_DIR="${ROOT_DIR}/.qa/evidence"
MULTI_FIXTURE="${ROOT_DIR}/tests/fixtures/capture/task13_multioutput_geometry.json"
FRACTIONAL_FIXTURE="${ROOT_DIR}/tests/fixtures/capture/task13_fractional_scaling.json"
MULTI_EVIDENCE="${EVIDENCE_DIR}/task-13-multioutput-geometry.json"
FRACTIONAL_EVIDENCE="${EVIDENCE_DIR}/task-13-fractional-scaling.json"

mkdir -p "${EVIDENCE_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

if [[ ! -f "${MULTI_FIXTURE}" ]]; then
  echo "ERR_QA_FIXTURE_MISSING path=${MULTI_FIXTURE}" >&2
  exit 1
fi

if [[ ! -f "${FRACTIONAL_FIXTURE}" ]]; then
  echo "ERR_QA_FIXTURE_MISSING path=${FRACTIONAL_FIXTURE}" >&2
  exit 1
fi

zig build test >/dev/null

jq -e 'all(.[]; has("name") and has("local") and has("output") and has("expected") and has("expected_runtime_arg"))' "${MULTI_FIXTURE}" >/dev/null
jq -e 'all(.[]; .expected.width > 0 and .expected.height > 0 and (.expected_runtime_arg | test("^-?[0-9]+,-?[0-9]+ [0-9]+x[0-9]+$")))' "${MULTI_FIXTURE}" >/dev/null
jq -e 'all(.[]; .expected.width > 0 and .expected.height > 0 and (.expected_runtime_arg | test("^-?[0-9]+,-?[0-9]+ [0-9]+x[0-9]+$")))' "${FRACTIONAL_FIXTURE}" >/dev/null

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
multi_cases="$(jq -r 'length' "${MULTI_FIXTURE}")"
fractional_cases="$(jq -r 'length' "${FRACTIONAL_FIXTURE}")"

jq -n \
  --arg timestamp "${timestamp}" \
  --arg fixture "tests/fixtures/capture/task13_multioutput_geometry.json" \
  --argjson fixture_cases "${multi_cases}" \
  '{
    suite: "task-13-multioutput-geometry",
    timestamp: $timestamp,
    pass: true,
    fixture: $fixture,
    checks: {
      fixture_shape_valid: true,
      runtime_arg_format_valid: true,
      zig_build_test_pass: true,
      fixture_cases: $fixture_cases
    }
  }' > "${MULTI_EVIDENCE}"

jq -n \
  --arg timestamp "${timestamp}" \
  --arg fixture "tests/fixtures/capture/task13_fractional_scaling.json" \
  --argjson fixture_cases "${fractional_cases}" \
  '{
    suite: "task-13-fractional-scaling",
    timestamp: $timestamp,
    pass: true,
    fixture: $fixture,
    checks: {
      non_negative_dimensions: true,
      runtime_arg_format_valid: true,
      zig_build_test_pass: true,
      fixture_cases: $fixture_cases
    }
  }' > "${FRACTIONAL_EVIDENCE}"

echo "ok overlay_geometry_fixtures multi=${MULTI_EVIDENCE} fractional=${FRACTIONAL_EVIDENCE}"
