#!/usr/bin/env bash
set -euo pipefail

architecture_file="${1:-}"
schema_file="${2:-}"

if [ -z "${architecture_file}" ] || [ ! -f "${architecture_file}" ]; then
  echo "ERR_CLI_CONTRACT_INVALID reason=missing_architecture_file file=${architecture_file:-unset}" >&2
  exit 1
fi

if [ -z "${schema_file}" ] || [ ! -f "${schema_file}" ]; then
  echo "ERR_CLI_CONTRACT_INVALID reason=missing_schema_file file=${schema_file:-unset}" >&2
  exit 1
fi

if ! grep -q '^## AGENT-FIRST CLI Contract$' "${architecture_file}"; then
  echo "ERR_CLI_CONTRACT_INVALID reason=missing_contract_section file=${architecture_file}" >&2
  exit 1
fi

if ! grep -q 'contract_version' "${architecture_file}"; then
  echo "ERR_CLI_CONTRACT_INVALID reason=missing_contract_version_field file=${architecture_file}" >&2
  exit 1
fi

if ! grep -q 'no human-only output is allowed on stdout' "${architecture_file}"; then
  echo "ERR_CLI_CONTRACT_INVALID reason=missing_machine_first_policy file=${architecture_file}" >&2
  exit 1
fi

for marker in 'shaula capture' 'shaula daemon' 'shaula capabilities' 'shaula history' 'shaula clipboard'; do
  if ! grep -q "${marker}" "${architecture_file}"; then
    echo "ERR_CLI_CONTRACT_INVALID reason=missing_command_family marker=${marker} file=${architecture_file}" >&2
    exit 1
  fi
done

if ! grep -q '^### Error Taxonomy (explicit `ERR_\*`)$' "${architecture_file}"; then
  echo "ERR_CLI_ERROR_TAXONOMY_INCOMPLETE reason=missing_error_taxonomy_section file=${architecture_file}" >&2
  exit 1
fi

for taxonomy_marker in \
  '^#### Capture `ERR_\*`$' \
  '^#### Daemon `ERR_\*`$' \
  '^#### Capabilities `ERR_\*`$' \
  '^#### History `ERR_\*`$' \
  '^#### Clipboard `ERR_\*`$'; do
  if ! grep -q "${taxonomy_marker}" "${architecture_file}"; then
    echo "ERR_CLI_ERROR_TAXONOMY_INCOMPLETE reason=missing_error_family marker=${taxonomy_marker} file=${architecture_file}" >&2
    exit 1
  fi
done

if ! grep -q '^  "oneOf": \[$' "${schema_file}"; then
  echo "ERR_CLI_SCHEMA_INVALID reason=missing_top_level_oneof file=${schema_file}" >&2
  exit 1
fi

for required_field in '"ok"' '"contract_version"' '"command"' '"timestamp"' '"result"' '"error"' '"code"'; do
  if ! grep -q "${required_field}" "${schema_file}"; then
    echo "ERR_CLI_SCHEMA_INVALID reason=missing_required_field field=${required_field} file=${schema_file}" >&2
    exit 1
  fi
done

echo "ok cli_contract file=${architecture_file} schema=${schema_file} commands=capture,daemon,capabilities,history,clipboard"
exit 0
