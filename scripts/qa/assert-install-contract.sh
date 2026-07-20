#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="${1:?source root is required}"
BUILD_ROOT="${2:?build root is required}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

STAGE="${TMP_DIR}/stage"
ARCHIVE="${TMP_DIR}/shaula-linux-x86_64.tar.gz"
ARM_ARCHIVE="${TMP_DIR}/shaula-linux-aarch64.tar.gz"
SUMS="${TMP_DIR}/SHA256SUMS"
DESTDIR="${STAGE}" meson install -C "${BUILD_ROOT}" >/dev/null
STAGED_MANIFEST="$(find "${STAGE}" -type f -path '*/share/shaula/release-manifest.txt' -print -quit)"
test -n "${STAGED_MANIFEST}"
PAYLOAD_ROOT="$(dirname "$(dirname "$(dirname "${STAGED_MANIFEST}")")")"
tar -C "${PAYLOAD_ROOT}" -czf "${ARCHIVE}" .
cp "${ARCHIVE}" "${ARM_ARCHIVE}"
(
  cd "${TMP_DIR}"
  sha256sum "$(basename "${ARCHIVE}")" "$(basename "${ARM_ARCHIVE}")" \
    > "$(basename "${SUMS}")"
)

run_installer() {
  local root="${1}"
  shift
  mkdir -p "${root}/home" "${root}/bin" "${root}/data" "${root}/config"
  env \
    HOME="${root}/home" \
    XDG_BIN_HOME="${root}/bin" \
    XDG_DATA_HOME="${root}/data" \
    XDG_CONFIG_HOME="${root}/config" \
    SHAULA_RELEASE_ASSET_URL="file://${ARCHIVE}" \
    SHAULA_SHA256SUMS_URL="file://${SUMS}" \
    "${SOURCE_ROOT}/scripts/install.sh" "$@"
}

portal_root="${TMP_DIR}/portal"
portal_output="$(
  SHAULA_COMPOSITOR=gnome \
  WAYLAND_DISPLAY=wayland-test \
  SHAULA_GRIM_AVAILABLE=0 \
  SHAULA_PORTAL_AVAILABLE=1 \
  SHAULA_SHORTCUTS_TEST_PORTAL=unsupported \
  run_installer "${portal_root}" --yes --no-integrations --no-icon
)"
grep -q 'installed: verified Wayland capture route' <<<"${portal_output}"
grep -q 'Shaula is ready' <<<"${portal_output}"
test -x "${portal_root}/bin/shaula-clipboard-provider"
test -x "${portal_root}/bin/shaula-shortcut-provider"
test -f "${portal_root}/data/icons/hicolor/scalable/actions/shaula-copy-symbolic.svg"
test ! -e "${portal_root}/data/icons/hicolor/scalable/apps/shaula.svg"
test ! -e "${portal_root}/data/icons/hicolor/256x256/apps/shaula.png"
test ! -e "${portal_root}/data/icons/hicolor/index.theme"
grep -q '^shortcuts_choice=enabled$' \
  "${portal_root}/config/shaula/setup-state.ini"
test ! -e "${portal_root}/config/autostart/dev.shaula.ShortcutProvider.desktop"

decline_root="${TMP_DIR}/decline"
decline_output="$(
  SHAULA_COMPOSITOR=gnome \
  WAYLAND_DISPLAY=wayland-test \
  SHAULA_GRIM_AVAILABLE=0 \
  SHAULA_PORTAL_AVAILABLE=1 \
  SHAULA_SHORTCUTS_TEST_PORTAL=unsupported \
  run_installer "${decline_root}" --yes --no-integrations --no-shortcuts --no-icon
)"
grep -q 'disabled: capture shortcuts (choice remembered)' <<<"${decline_output}"
grep -q '^shortcuts_choice=declined$' \
  "${decline_root}/config/shaula/setup-state.ini"
test ! -e "${decline_root}/config/autostart/dev.shaula.ShortcutProvider.desktop"

set +e
conflict_output="$(run_installer "${TMP_DIR}/flag-conflict" \
  --shortcuts --no-shortcuts 2>&1)"
conflict_status=$?
set -e
test "${conflict_status}" -ne 0
grep -q -- '--no-shortcuts conflicts with --shortcuts' <<<"${conflict_output}"

assert_arch_selection() {
  local machine="$1"
  local expected_arch="$2"
  local root="${TMP_DIR}/arch-${expected_arch}"
  local fake_bin="${root}/fake-bin"
  local download_log="${root}/downloads.log"
  mkdir -p "${root}/home" "${root}/bin" "${root}/data" \
    "${root}/config" "${fake_bin}"

  cat > "${fake_bin}/uname" <<EOF
#!/bin/sh
printf '%s\\n' '${machine}'
EOF
  cat > "${fake_bin}/curl" <<'EOF'
#!/bin/sh
set -eu
url=''
output=''
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) shift; output="$1" ;;
    http://*|https://*) url="$1" ;;
  esac
  shift
done
printf '%s\n' "$url" >> "${SHAULA_TEST_DOWNLOAD_LOG}"
case "$url" in
  */shaula-linux-x86_64.tar.gz) cp "${SHAULA_TEST_ARCHIVE_X86}" "$output" ;;
  */shaula-linux-aarch64.tar.gz) cp "${SHAULA_TEST_ARCHIVE_ARM}" "$output" ;;
  */SHA256SUMS) cp "${SHAULA_TEST_SUMS}" "$output" ;;
  *) printf 'unexpected URL: %s\n' "$url" >&2; exit 1 ;;
esac
EOF
  chmod +x "${fake_bin}/uname" "${fake_bin}/curl"

  env \
    HOME="${root}/home" \
    PATH="${fake_bin}:${PATH}" \
    XDG_BIN_HOME="${root}/bin" \
    XDG_DATA_HOME="${root}/data" \
    XDG_CONFIG_HOME="${root}/config" \
    SHAULA_VERSION=v0.1.6 \
    SHAULA_COMPOSITOR=gnome \
    WAYLAND_DISPLAY=wayland-test \
    SHAULA_GRIM_AVAILABLE=0 \
    SHAULA_PORTAL_AVAILABLE=1 \
    SHAULA_TEST_DOWNLOAD_LOG="${download_log}" \
    SHAULA_TEST_ARCHIVE_X86="${ARCHIVE}" \
    SHAULA_TEST_ARCHIVE_ARM="${ARM_ARCHIVE}" \
    SHAULA_TEST_SUMS="${SUMS}" \
    "${SOURCE_ROOT}/scripts/install.sh" --yes --no-integrations --no-icon \
    >/dev/null

  grep -Fq "/v0.1.6/shaula-linux-${expected_arch}.tar.gz" "${download_log}"
  if [[ ${expected_arch} == x86_64 ]]; then
    ! grep -Fq 'shaula-linux-aarch64.tar.gz' "${download_log}"
  else
    ! grep -Fq 'shaula-linux-x86_64.tar.gz' "${download_log}"
  fi
}

assert_arch_selection x86_64 x86_64
assert_arch_selection amd64 x86_64
assert_arch_selection aarch64 aarch64
assert_arch_selection arm64 aarch64

missing_root="${TMP_DIR}/missing"
set +e
missing_output="$(
  SHAULA_COMPOSITOR=niri \
  WAYLAND_DISPLAY=wayland-test \
  SHAULA_GRIM_AVAILABLE=0 \
  SHAULA_PORTAL_AVAILABLE=0 \
  run_installer "${missing_root}" --yes --no-integrations 2>&1
)"
missing_rc=$?
set -e
test "${missing_rc}" -ne 0
grep -q 'no usable screenshot route' <<<"${missing_output}"
test ! -e "${missing_root}/bin/shaula"
test ! -e "${missing_root}/data/applications/shaula.desktop"

setup_root="${TMP_DIR}/setup"
mkdir -p \
  "${setup_root}/home" \
  "${setup_root}/config/niri" \
  "${setup_root}/config/noctalia"
printf '%s\n' '// setup contract' > "${setup_root}/config/niri/config.kdl"
printf '%s\n' '{"version":2,"states":{}}' > \
  "${setup_root}/config/noctalia/plugins.json"
printf '%s\n' \
  '{"bar":{"widgets":{"left":[],"center":[],"right":[]}}}' > \
  "${setup_root}/config/noctalia/settings.json"

setup_env=(
  HOME="${setup_root}/home"
  XDG_CONFIG_HOME="${setup_root}/config"
  SHAULA_NOCTALIA_PLUGIN_SOURCE="${SOURCE_ROOT}/integrations/noctalia/shaula"
  SHAULA_SHORTCUTS_TEST_PORTAL=unsupported
)
setup_first="$(env "${setup_env[@]}" "${BUILD_ROOT}/shaula" setup \
  --yes)"
grep -q 'skipped: capture shortcuts (XDG GlobalShortcuts portal unavailable' <<<"${setup_first}"
grep -q 'installed: Niri preview rule' <<<"${setup_first}"
grep -q 'installed: Noctalia integration' <<<"${setup_first}"
setup_second="$(env "${setup_env[@]}" "${BUILD_ROOT}/shaula" setup \
  --yes)"
grep -q 'skipped: capture shortcuts (unsupported)' <<<"${setup_second}"
grep -q 'unchanged: Niri preview rule' <<<"${setup_second}"
grep -q 'unchanged: Noctalia integration' <<<"${setup_second}"

remove_first="$(env "${setup_env[@]}" "${BUILD_ROOT}/shaula" setup \
  --remove --yes)"
grep -q 'removed: capture shortcuts (disabled)' <<<"${remove_first}"
grep -q 'removed: Niri preview rule' <<<"${remove_first}"
grep -q 'removed: Noctalia integration' <<<"${remove_first}"
remove_second="$(env "${setup_env[@]}" "${BUILD_ROOT}/shaula" setup \
  --remove --yes)"
grep -q 'removed: capture shortcuts (disabled)' <<<"${remove_second}"
grep -q 'unchanged: Niri preview rule' <<<"${remove_second}"
grep -q 'unchanged: Noctalia integration' <<<"${remove_second}"

if grep -Eq '(^|[^[:alnum:]_])(sudo|pacman)([^[:alnum:]_]|$)' \
  "${SOURCE_ROOT}/scripts/install.sh"; then
  echo 'public installer contains a privilege or package-manager path' >&2
  exit 1
fi

printf 'ok install contracts\n'
