#!/usr/bin/env sh
set -eu

REPO_URL="https://github.com/fgonzalezurriola/shaula"
INSTALL_VERSION="${SHAULA_VERSION:-}"
ASSUME_YES=0
INSTALL_INTEGRATIONS=1
INSTALL_DESKTOP=1
INSTALL_APP_ICONS=1
INSTALL_SHORTCUTS=""
INSTALL_NOCTALIA=0
UNINSTALL=0
INSTALL_CONTEXT="${SHAULA_INSTALL_CONTEXT:-release}"

XDG_BIN_HOME="${XDG_BIN_HOME:-${HOME}/.local/bin}"
XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${HOME}/.config}"
SHAULA_CONFIG_DIR="${XDG_CONFIG_HOME}/shaula"
INSTALLED_MANIFEST="${XDG_DATA_HOME}/shaula/release-manifest.txt"

if [ -t 2 ] && [ "${TERM:-dumb}" != "dumb" ]; then
  RED="$(printf '\033[1;31m')"
  RESET="$(printf '\033[0m')"
else
  RED=""
  RESET=""
fi

usage() {
  cat <<'EOF'
Usage: scripts/install.sh [options]
       scripts/install.sh v0.1.8

Install Shaula for the current user. The installer verifies the complete release
payload and a working Wayland capture route before changing user files. It never
installs system packages or elevates privileges.

Options:
  --help              Show this help.
  --version <tag>     Install a specific GitHub release tag.
  --yes               Advanced noninteractive automation mode.
  --no-integrations   Skip optional Niri and Noctalia setup.
  --no-desktop        Skip the desktop entry.
  --no-icon           Skip application icons; Preview runtime icons remain required.
  --shortcuts         Enable recommended Ctrl+Shift+1–4 shortcuts.
  --no-shortcuts      Remember that automatic shortcuts are declined.
  --noctalia          Explicitly install detected Noctalia integration.
  --uninstall         Remove files installed by this script and integrations.

Environment:
  SHAULA_VERSION             Install a specific release tag.
  SHAULA_RELEASE_ASSET_URL   Override the release archive URL.
  SHAULA_SHA256SUMS_URL      Override the SHA256SUMS URL.
  SHAULA_INSTALL_CONTEXT     Set to dev for local development messaging.
EOF
}

status() {
  state="$1"
  shift
  printf '%s: %s\n' "$state" "$*"
}

fail() {
  printf '%sERROR: %s%s\n' "$RED" "$*" "$RESET" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

can_prompt() {
  [ -r /dev/tty ] && [ -w /dev/tty ]
}

prompt_yes_no() {
  question="$1"
  can_prompt || return 1
  printf '%s [y/N] ' "$question" > /dev/tty
  IFS= read -r answer < /dev/tty || return 1
  case "$answer" in
    y|Y|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

confirm_dev_install() {
  if [ "$INSTALL_CONTEXT" != "dev" ] || [ "$ASSUME_YES" -eq 1 ]; then
    return 0
  fi
  prompt_yes_no "Install this local Shaula build into ${XDG_BIN_HOME} and ${XDG_DATA_HOME}?" ||
    fail "installation cancelled"
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64) printf 'x86_64' ;;
    aarch64|arm64) printf 'aarch64' ;;
    *) fail "unsupported architecture: $(uname -m)" ;;
  esac
}

asset_url() {
  arch="$1"
  if [ -n "${SHAULA_RELEASE_ASSET_URL:-}" ]; then
    printf '%s' "$SHAULA_RELEASE_ASSET_URL"
  elif [ -n "$INSTALL_VERSION" ]; then
    printf '%s/releases/download/%s/shaula-linux-%s.tar.gz' \
      "$REPO_URL" "$INSTALL_VERSION" "$arch"
  else
    printf '%s/releases/latest/download/shaula-linux-%s.tar.gz' \
      "$REPO_URL" "$arch"
  fi
}

sha256sums_url() {
  if [ -n "${SHAULA_SHA256SUMS_URL:-}" ]; then
    printf '%s' "$SHAULA_SHA256SUMS_URL"
  elif [ -n "$INSTALL_VERSION" ]; then
    printf '%s/releases/download/%s/SHA256SUMS' "$REPO_URL" "$INSTALL_VERSION"
  else
    printf '%s/releases/latest/download/SHA256SUMS' "$REPO_URL"
  fi
}

download() {
  url="$1"
  output="$2"
  case "$url" in
    file://*) cp "${url#file://}" "$output" ;;
    *)
      if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$output"
      elif command -v wget >/dev/null 2>&1; then
        wget -qO "$output" "$url"
      else
        fail "curl or wget is required"
      fi
      ;;
  esac
}

verify_checksum() {
  archive="$1"
  sums="$2"
  archive_name="$(basename "$archive")"
  expected="$(awk -v name="$archive_name" '$2 == name || $2 == "*" name { print; exit }' "$sums")"
  [ -n "$expected" ] || fail "SHA256SUMS does not contain ${archive_name}"
  printf '%s\n' "$expected" > "${archive}.sha256"
  (cd "$(dirname "$archive")" && sha256sum -c "$(basename "${archive}.sha256")") >/dev/null ||
    fail "release checksum verification failed"
  status installed "verified release checksum"
}

verify_release_payload() {
  root="$1"
  manifest="${root}/share/shaula/release-manifest.txt"
  [ -f "$manifest" ] || fail "release archive is missing its payload manifest"
  while IFS= read -r relative || [ -n "$relative" ]; do
    case "$relative" in
      ''|'#'*) continue ;;
      /*|../*|*/../*|*/..) fail "invalid payload manifest path: $relative" ;;
    esac
    path="${root}/${relative}"
    [ -f "$path" ] || fail "release archive is incomplete: missing ${relative}"
    [ ! -L "$path" ] || fail "release archive contains an unexpected symlink: ${relative}"
    case "$relative" in
      bin/*) [ -x "$path" ] || fail "release helper is not executable: ${relative}" ;;
    esac
  done < "$manifest"
  status installed "verified complete release payload"
}

validate_capture_environment() {
  root="$1"
  shaula="${root}/bin/shaula"
  output=""
  if output="$(PATH="${root}/bin:${PATH}" "$shaula" capabilities list --json 2>&1)"; then
    status installed "verified Wayland capture route"
    return 0
  fi
  printf '%s\n' "$output" >&2
  if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    fail "no active Wayland session was detected; run the installer from your Wayland desktop session"
  fi
  fail "this Wayland session exposes no usable screenshot route; enable the desktop screenshot portal or provide grim on a compatible compositor"
}

install_file() {
  source="$1"
  destination="$2"
  mode="$3"
  report="${4:-1}"
  mkdir -p "$(dirname "$destination")"
  if [ -f "$destination" ] && cmp -s "$source" "$destination"; then
    chmod "$mode" "$destination"
    [ "$report" -eq 0 ] || status unchanged "$destination"
    return 0
  fi
  install -m "$mode" "$source" "$destination"
  [ "$report" -eq 0 ] || status installed "$destination"
}

should_install_manifest_path() {
  relative="$1"
  case "$relative" in
    share/applications/shaula.desktop)
      [ "$INSTALL_DESKTOP" -eq 1 ]
      ;;
    share/icons/hicolor/*/apps/shaula.png|share/icons/hicolor/scalable/apps/shaula.svg)
      [ "$INSTALL_APP_ICONS" -eq 1 ]
      ;;
    *) return 0 ;;
  esac
}

install_payload() {
  root="$1"
  manifest="${root}/share/shaula/release-manifest.txt"
  while IFS= read -r relative || [ -n "$relative" ]; do
    case "$relative" in ''|'#'*) continue ;; esac
    if ! should_install_manifest_path "$relative"; then
      status skipped "$relative"
      continue
    fi
    source="${root}/${relative}"
    report=1
    case "$relative" in
      bin/*)
        destination="${XDG_BIN_HOME}/${relative#bin/}"
        mode=0755
        ;;
      share/icons/hicolor/scalable/actions/*)
        destination="${XDG_DATA_HOME}/${relative#share/}"
        mode=0644
        report=0
        ;;
      share/*)
        destination="${XDG_DATA_HOME}/${relative#share/}"
        mode=0644
        ;;
      *) fail "unsupported payload path: $relative" ;;
    esac
    install_file "$source" "$destination" "$mode" "$report"
  done < "$manifest"
  status installed "Preview runtime icons"
  install_file "$manifest" "$INSTALLED_MANIFEST" 0644
}

refresh_desktop_caches() {
  icon_dir="${XDG_DATA_HOME}/icons/hicolor"
  applications_dir="${XDG_DATA_HOME}/applications"
  if [ -d "$icon_dir" ] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    if gtk-update-icon-cache -f -t "$icon_dir" >/dev/null 2>&1; then
      status installed "refreshed icon cache"
    else
      status skipped "icon cache refresh failed" >&2
    fi
  fi
  if [ -d "$applications_dir" ] && command -v update-desktop-database >/dev/null 2>&1; then
    if update-desktop-database -q "$applications_dir" >/dev/null 2>&1; then
      status installed "refreshed desktop database"
    else
      status skipped "desktop database refresh failed" >&2
    fi
  fi
}

run_setup() {
  shaula="${XDG_BIN_HOME}/shaula"
  source="${XDG_DATA_HOME}/shaula/integrations/noctalia/shaula"
  args=""
  if [ "$ASSUME_YES" -eq 1 ]; then
    args="--yes"
  fi
  if [ "$INSTALL_INTEGRATIONS" -eq 0 ]; then
    args="${args} --no-integrations"
  fi
  if [ "$INSTALL_SHORTCUTS" = "enable" ]; then
    args="${args} --shortcuts"
  elif [ "$INSTALL_SHORTCUTS" = "disable" ]; then
    args="${args} --no-shortcuts"
  fi
  if [ "$INSTALL_NOCTALIA" -eq 1 ]; then
    args="${args} --noctalia"
  fi

  if SHAULA_NOCTALIA_PLUGIN_SOURCE="$source" "$shaula" setup $args; then
    status installed "user setup"
  else
    fail "user setup failed; immutable application files were installed but optional user state is incomplete"
  fi
}

validate_installed_core() {
  shaula="${XDG_BIN_HOME}/shaula"
  [ -x "$shaula" ] || fail "installed shaula binary is missing"
  command -v wl-copy >/dev/null 2>&1 || fail "wl-copy is required for clipboard publication"
  [ -f "${XDG_DATA_HOME}/icons/hicolor/scalable/actions/shaula-copy-symbolic.svg" ] ||
    fail "Preview runtime icons are incomplete"
  [ -f "${XDG_DATA_HOME}/shaula/integrations/noctalia/shaula/manifest.json" ] ||
    fail "distributed integration payload is incomplete"
  PATH="${XDG_BIN_HOME}:${PATH}" "$shaula" capabilities list --json >/dev/null ||
    fail "installed capture route validation failed"
  PATH="${XDG_BIN_HOME}:${PATH}" "$shaula" preflight --json >/dev/null ||
    fail "installed Wayland preflight failed"
  PATH="${XDG_BIN_HOME}:${PATH}" "$shaula" doctor --json >/dev/null ||
    fail "installed runtime diagnostics failed"
  status installed "validated capture route, Preview resources, and wl-copy"
}

remove_path() {
  path="$1"
  if [ -e "$path" ] || [ -L "$path" ]; then
    rm -rf "$path"
    status installed "removed $path"
  else
    status unchanged "$path"
  fi
}

uninstall_payload() {
  shaula="${XDG_BIN_HOME}/shaula"
  if [ -x "$shaula" ]; then
    "$shaula" setup --remove --yes ||
      status failed "optional integration removal was incomplete" >&2
  fi

  if [ -f "$INSTALLED_MANIFEST" ]; then
    while IFS= read -r relative || [ -n "$relative" ]; do
      case "$relative" in ''|'#'*) continue ;; esac
      case "$relative" in
        bin/*) target="${XDG_BIN_HOME}/${relative#bin/}" ;;
        share/*) target="${XDG_DATA_HOME}/${relative#share/}" ;;
        *) continue ;;
      esac
      remove_path "$target"
    done < "$INSTALLED_MANIFEST"
  else
    for binary in shaula shaula-overlay shaula-preview shaula-settings \
      shaula-shortcut-provider shaula-crop-image shaula-portal-screenshot \
      shaula-clipboard-provider; do
      remove_path "${XDG_BIN_HOME}/${binary}"
    done
    remove_path "${XDG_DATA_HOME}/applications/shaula.desktop"
    remove_path "${XDG_DATA_HOME}/shaula/integrations/noctalia/shaula"
  fi
  remove_path "$INSTALLED_MANIFEST"
  refresh_desktop_caches
  status unchanged "kept ${SHAULA_CONFIG_DIR}/config.toml"
  status installed "uninstall complete"
}

install_release() {
  for command in uname tar awk sha256sum install mktemp cmp chmod; do
    need_cmd "$command"
  done
  need_cmd wl-copy

  arch="$(detect_arch)"
  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "$tmp_dir"' EXIT INT HUP TERM
  archive="${tmp_dir}/shaula-linux-${arch}.tar.gz"
  sums="${tmp_dir}/SHA256SUMS"
  extract="${tmp_dir}/extract"

  status installed "downloading Shaula release"
  download "$(asset_url "$arch")" "$archive"
  download "$(sha256sums_url)" "$sums"
  verify_checksum "$archive" "$sums"
  mkdir -p "$extract"
  tar -xzf "$archive" -C "$extract"
  verify_release_payload "$extract"
  validate_capture_environment "$extract"
  confirm_dev_install

  install_payload "$extract"
  refresh_desktop_caches
  run_setup
  validate_installed_core

  printf '\nShaula is ready. Try:\n  %s capture area --json\n' \
    "${XDG_BIN_HOME}/shaula"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --version)
      shift
      [ "$#" -gt 0 ] || fail "--version requires a tag"
      INSTALL_VERSION="$1"
      ;;
    --yes) ASSUME_YES=1 ;;
    --no-integrations) INSTALL_INTEGRATIONS=0 ;;
    --no-desktop) INSTALL_DESKTOP=0 ;;
    --no-icon) INSTALL_APP_ICONS=0 ;;
    --shortcuts)
      [ "$INSTALL_SHORTCUTS" != "disable" ] ||
        fail "--shortcuts conflicts with --no-shortcuts"
      INSTALL_SHORTCUTS=enable
      ;;
    --no-shortcuts)
      [ "$INSTALL_SHORTCUTS" != "enable" ] ||
        fail "--no-shortcuts conflicts with --shortcuts"
      INSTALL_SHORTCUTS=disable
      ;;
    --noctalia) INSTALL_NOCTALIA=1 ;;
    --uninstall) UNINSTALL=1 ;;
    v*)
      [ -z "$INSTALL_VERSION" ] || fail "version specified more than once"
      INSTALL_VERSION="$1"
      ;;
    *) fail "unknown option: $1" ;;
  esac
  shift
done

if [ "$UNINSTALL" -eq 1 ]; then
  uninstall_payload
else
  install_release
fi
