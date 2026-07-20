#!/usr/bin/env bash
set -euo pipefail

shaula="${1:?shaula binary required}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
mkdir -p "${tmp}/config" "${tmp}/state" "${tmp}/runtime/shaula" "${tmp}/bin"
log="${tmp}/settings.log"
launcher_log="${tmp}/launcher.log"
cat > "${tmp}/bin/settings-helper" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "${SHAULA_TEST_SETTINGS_LOG}"
EOF
chmod +x "${tmp}/bin/settings-helper"
cat > "${tmp}/bin/launcher-helper" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "${SHAULA_TEST_LAUNCHER_LOG}"
EOF
chmod +x "${tmp}/bin/launcher-helper"

env_common=(
  HOME="${tmp}"
  XDG_CONFIG_HOME="${tmp}/config"
  XDG_STATE_HOME="${tmp}/state"
  XDG_RUNTIME_DIR="${tmp}/runtime"
  SHAULA_SHORTCUTS_TEST_PORTAL=unsupported
  SHAULA_SETTINGS_HELPER_BIN="${tmp}/bin/settings-helper"
  SHAULA_LAUNCHER_HELPER_BIN="${tmp}/bin/launcher-helper"
  SHAULA_TEST_SETTINGS_LOG="${log}"
  SHAULA_TEST_LAUNCHER_LOG="${launcher_log}"
)

env "${env_common[@]}" "${shaula}" launch
test ! -s "${log}"
grep -Fxq -- "${shaula}" "${launcher_log}"

printf 'ok launch menu routing\n'
