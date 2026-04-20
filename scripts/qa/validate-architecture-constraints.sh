#!/usr/bin/env bash
set -euo pipefail

target_file="${1:-}"

if [ -z "${target_file}" ] || [ ! -f "${target_file}" ]; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_target_file file=${target_file:-unset}" >&2
  exit 1
fi

for required_section in '^## Process Topology$' '^## IPC Contract v1$' '^## Plugin Optionality Rule$'; do
  if ! grep -q "${required_section}" "${target_file}"; then
    echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_required_section marker=${required_section} file=${target_file}" >&2
    exit 1
  fi
done

if ! grep -qi 'daemon is the source of truth' "${target_file}"; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_daemon_source_of_truth file=${target_file}" >&2
  exit 1
fi

if ! grep -qi 'capture path must work without plugin presence' "${target_file}"; then
  echo "ERR_PLUGIN_HARD_DEPENDENCY reason=missing_hot_path_plugin_independence_rule file=${target_file}" >&2
  exit 1
fi

if ! grep -qi 'optional and non-blocking' "${target_file}"; then
  echo "ERR_PLUGIN_HARD_DEPENDENCY reason=missing_plugin_optionality_phrase file=${target_file}" >&2
  exit 1
fi

# Explicit hard-dependency anti-patterns.
if grep -Eqi 'noctalia[^\n]*\b(required|mandatory|must be installed|hard dependency)\b' "${target_file}"; then
  echo "ERR_PLUGIN_HARD_DEPENDENCY reason=explicit_noctalia_requirement_detected file=${target_file}" >&2
  exit 1
fi

if grep -Eqi '(capture|daemon startup|preflight)[^\n]*\b(requires|depends on)\b[^\n]*noctalia' "${target_file}"; then
  echo "ERR_PLUGIN_HARD_DEPENDENCY reason=core_flow_depends_on_noctalia file=${target_file}" >&2
  exit 1
fi

if ! grep -q 'daemon-v1.sock' "${target_file}"; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_socket_path_rule file=${target_file}" >&2
  exit 1
fi

if ! grep -q 'ipc_version' "${target_file}"; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_ipc_version_field file=${target_file}" >&2
  exit 1
fi

if ! grep -q 'ERR_IPC_TIMEOUT' "${target_file}"; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_ipc_timeout_policy file=${target_file}" >&2
  exit 1
fi

if ! grep -q 'daemon.status' "${target_file}"; then
  echo "ERR_ARCHITECTURE_CONSTRAINTS_INVALID reason=missing_health_check_contract file=${target_file}" >&2
  exit 1
fi

echo "ok architecture_constraints file=${target_file} topology=present ipc=v1 plugin=optional_non_blocking"
exit 0
