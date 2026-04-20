#!/usr/bin/env bash
set -euo pipefail

target_file="${1:-}"

if [ -z "${target_file}" ] || [ ! -f "${target_file}" ]; then
  echo "ERR_CAPABILITY_MATRIX_INCOMPLETE reason=missing_target_file file=${target_file:-unset}" >&2
  exit 1
fi

if ! grep -q '^## MVP Capability Matrix$' "${target_file}"; then
  echo "ERR_CAPABILITY_MATRIX_INCOMPLETE reason=missing_matrix_section file=${target_file}" >&2
  exit 1
fi

if ! grep -q '^| Feature | Protocol/Path | Status | Fallback | Risk |$' "${target_file}"; then
  echo "ERR_CAPABILITY_MATRIX_INCOMPLETE reason=missing_matrix_header file=${target_file}" >&2
  exit 1
fi

if ! grep -q '^| --- | --- | --- | --- | --- |$' "${target_file}"; then
  echo "ERR_CAPABILITY_MATRIX_INCOMPLETE reason=missing_matrix_separator file=${target_file}" >&2
  exit 1
fi

row_count="$(grep -c '^| .* | .* | .* | .* | .* |$' "${target_file}")"

# header + separator + at least one data row = 3 rows minimum
if [ "${row_count}" -lt 3 ]; then
  echo "ERR_CAPABILITY_MATRIX_INCOMPLETE reason=missing_matrix_rows file=${target_file}" >&2
  exit 1
fi

echo "ok capability_matrix file=${target_file} columns=feature,protocol_path,status,fallback,risk"
exit 0
