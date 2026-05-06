#!/usr/bin/env sh
set -eu

REPO_URL="https://github.com/fgonzalezurriola/shaula"
INSTALL_VERSION=""
ASSUME_YES=0
INSTALL_INTEGRATIONS=1
INSTALL_DESKTOP=1
INSTALL_ICON=1
UNINSTALL=0

XDG_BIN_HOME="${XDG_BIN_HOME:-${HOME}/.local/bin}"
XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${HOME}/.config}"
SHAULA_CONFIG_DIR="${XDG_CONFIG_HOME}/shaula"
SHAULA_GENERATED_DIR="${SHAULA_CONFIG_DIR}/generated"

usage() {
  cat <<'EOF'
Usage: scripts/install.sh [options]

Install Shaula for the current user. This script never uses sudo and never
overwrites an existing ~/.config/shaula/config.toml.

Options:
  --help              Show this help.
  --version <tag>     Install a specific GitHub release tag.
  --yes               Do not prompt before installing.
  --no-integrations   Skip generated integration snippets.
  --no-desktop        Skip the desktop entry.
  --no-icon           Skip the hicolor app icon.
  --uninstall         Remove files installed by this script.

Environment:
  SHAULA_RELEASE_ASSET_URL   Override release archive URL.
  SHAULA_SHA256SUMS_URL      Override SHA256SUMS URL.
EOF
}

log() {
  printf '%s\n' "$*"
}

warn() {
  printf 'warning: %s\n' "$*" >&2
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

confirm() {
  if [ "$ASSUME_YES" -eq 1 ]; then
    return 0
  fi
  printf 'Install Shaula into %s, %s, and %s? [y/N] ' "$XDG_BIN_HOME" "$XDG_DATA_HOME" "$SHAULA_CONFIG_DIR"
  read answer
  case "$answer" in
    y|Y|yes|YES) return 0 ;;
    *) die "installation cancelled" ;;
  esac
}

detect_arch() {
  machine="$(uname -m)"
  case "$machine" in
    x86_64|amd64) printf 'x86_64' ;;
    aarch64|arm64) printf 'aarch64' ;;
    *) die "unsupported architecture: $machine" ;;
  esac
}

latest_asset_base() {
  arch="$1"
  printf '%s/releases/latest/download/shaula-linux-%s.tar.gz' "$REPO_URL" "$arch"
}

tagged_asset_base() {
  arch="$1"
  tag="$2"
  printf '%s/releases/download/%s/shaula-linux-%s.tar.gz' "$REPO_URL" "$tag" "$arch"
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

asset_url() {
  arch="$1"
  if [ -n "${SHAULA_RELEASE_ASSET_URL:-}" ]; then
    printf '%s' "$SHAULA_RELEASE_ASSET_URL"
  elif [ -n "$INSTALL_VERSION" ]; then
    tagged_asset_base "$arch" "$INSTALL_VERSION"
  else
    latest_asset_base "$arch"
  fi
}

download() {
  url="$1"
  dest="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fL "$url" -o "$dest"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$dest" "$url"
  else
    die "missing required command: curl or wget"
  fi
}

verify_sha256sums() {
  archive="$1"
  sums="$2"
  archive_name="$(basename "$archive")"
  expected_line="$(awk -v name="$archive_name" '
    {
      file = $2
      sub(/^\*/, "", file)
      sub(/^\.\//, "", file)
      if (file == name) {
        print $1 "  " name
        found = 1
        exit
      }
    }
    END { if (!found) exit 1 }
  ' "$sums" || true)"
  [ -n "$expected_line" ] || die "SHA256SUMS does not contain ${archive_name}"
  printf '%s\n' "$expected_line" > "${archive}.sha256"
  (cd "$(dirname "$archive")" && sha256sum -c "$(basename "${archive}.sha256")") >/dev/null
}

install_file_if_present() {
  source="$1"
  dest="$2"
  mode="$3"
  if [ -f "$source" ]; then
    mkdir -p "$(dirname "$dest")"
    install -m "$mode" "$source" "$dest"
    log "installed $dest"
  fi
}

find_file() {
  root="$1"
  name="$2"
  find "$root" -type f -name "$name" | head -n 1
}

write_desktop_file() {
  dest="${XDG_DATA_HOME}/applications/shaula.desktop"
  mkdir -p "$(dirname "$dest")"
  cat > "$dest" <<EOF
[Desktop Entry]
Type=Application
Name=Shaula
Comment=Wayland screenshot tool for Niri
Exec=${XDG_BIN_HOME}/shaula capture all-in-one --json
Icon=shaula
Terminal=false
Categories=Graphics;Utility;
StartupNotify=false
EOF
  log "installed $dest"
}

write_icon_file() {
  dest="${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg"
  mkdir -p "$(dirname "$dest")"
  cat > "$dest" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <rect x="18" y="28" width="92" height="72" rx="14" fill="#1f2937"/>
  <rect x="30" y="40" width="68" height="48" rx="7" fill="#f8fafc"/>
  <path d="M42 74h22l9-13 13 19" fill="none" stroke="#0f172a" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="80" cy="56" r="7" fill="#0f172a"/>
</svg>
EOF
  log "installed $dest"
}

write_default_config() {
  dest="${SHAULA_CONFIG_DIR}/config.toml"
  mkdir -p "$SHAULA_CONFIG_DIR" "$SHAULA_GENERATED_DIR"
  if [ -e "$dest" ]; then
    log "kept existing $dest"
    return 0
  fi
  cat > "$dest" <<'EOF'
[capture]
region_capture_mode = "live"

[preview.window]
mode = "floating"
EOF
  log "created $dest"
}

detect_niri_config() {
  if [ -n "${NIRI_CONFIG:-}" ] && [ -e "$NIRI_CONFIG" ]; then
    printf '%s' "$NIRI_CONFIG"
    return 0
  fi
  for path in \
    "${HOME}/.config/niri/config.kdl" \
    "${HOME}/.config/niri/cfg" \
    "/etc/niri/config.kdl"
  do
    if [ -e "$path" ]; then
      printf '%s' "$path"
      return 0
    fi
  done
  return 1
}

detect_noctalia() {
  for path in \
    "${HOME}/.config/noctalia" \
    "${HOME}/.config/noctalia/plugins" \
    "${HOME}/.config/noctalia/plugins.json" \
    "${HOME}/.config/noctalia/settings.json"
  do
    if [ -e "$path" ]; then
      printf '%s' "$path"
      return 0
    fi
  done
  return 1
}

kdl_string() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

write_niri_snippet() {
  detected="$1"
  dest="${SHAULA_GENERATED_DIR}/niri-shaula.kdl"
  shaula_path="$(kdl_string "${XDG_BIN_HOME}/shaula")"
  detected_path="$(kdl_string "$detected")"
  mkdir -p "$SHAULA_GENERATED_DIR"
  cat > "$dest" <<EOF
// Generated by Shaula installer.
// Include or copy this manually from your Niri config if desired.
// Detected Niri config candidate: ${detected_path}

window-rule {
    match app-id="^dev\\.shaula\\.preview$"
    open-floating true
}

binds {
    Print { spawn "${shaula_path}" "capture" "area" "--json"; }
    Alt+Print { spawn "${shaula_path}" "capture" "fullscreen" "--json"; }
    Ctrl+Print { spawn "${shaula_path}" "capture" "focused" "--json"; }
}
EOF
  log "generated $dest"
}

warn_runtime_tools() {
  for tool in grim slurp wl-copy niri quickshell; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      warn "runtime tool not found in PATH: $tool"
    fi
  done
}

install_release() {
  need_cmd uname
  need_cmd tar
  need_cmd find
  need_cmd awk
  need_cmd sed
  need_cmd sha256sum
  need_cmd install
  need_cmd mktemp

  arch="$(detect_arch)"
  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "$tmp_dir"' EXIT INT HUP TERM
  archive="${tmp_dir}/shaula-linux-${arch}.tar.gz"
  sums="${tmp_dir}/SHA256SUMS"

  log "downloading $(asset_url "$arch")"
  download "$(asset_url "$arch")" "$archive"
  log "downloading $(sha256sums_url)"
  download "$(sha256sums_url)" "$sums"
  verify_sha256sums "$archive" "$sums"

  extract_dir="${tmp_dir}/extract"
  mkdir -p "$extract_dir"
  tar -xzf "$archive" -C "$extract_dir"

  shaula_bin="$(find_file "$extract_dir" shaula)"
  [ -n "$shaula_bin" ] || die "release archive does not contain shaula"

  confirm
  mkdir -p "$XDG_BIN_HOME" "$SHAULA_GENERATED_DIR"
  install -m 0755 "$shaula_bin" "${XDG_BIN_HOME}/shaula"
  log "installed ${XDG_BIN_HOME}/shaula"
  install_file_if_present "$(find_file "$extract_dir" shaula-overlay)" "${XDG_BIN_HOME}/shaula-overlay" 0755
  install_file_if_present "$(find_file "$extract_dir" shaula-preview)" "${XDG_BIN_HOME}/shaula-preview" 0755

  if [ "$INSTALL_DESKTOP" -eq 1 ]; then
    write_desktop_file
  fi
  if [ "$INSTALL_ICON" -eq 1 ]; then
    bundled_icon="$(find "$extract_dir" -type f -path '*/icons/hicolor/scalable/apps/shaula.svg' | head -n 1)"
    if [ -n "$bundled_icon" ]; then
      install_file_if_present "$bundled_icon" "${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg" 0644
    else
      write_icon_file
    fi
  fi

  write_default_config
  warn_runtime_tools

  if [ "$INSTALL_INTEGRATIONS" -eq 1 ]; then
    if niri_path="$(detect_niri_config)"; then
      log "detected Niri config candidate: $niri_path"
      write_niri_snippet "$niri_path"
      log "Niri config was not edited automatically."
    else
      log "Niri config was not detected; no Niri snippet generated."
    fi

    if noctalia_path="$(detect_noctalia)"; then
      log "detected Noctalia candidate: $noctalia_path"
      log "Noctalia plugins.json was not edited automatically."
    fi
  fi

  log "Shaula install complete."
}

remove_path() {
  path="$1"
  if [ -e "$path" ] || [ -L "$path" ]; then
    rm -rf "$path"
    log "removed $path"
  fi
}

run_uninstall() {
  remove_path "${XDG_BIN_HOME}/shaula"
  remove_path "${XDG_BIN_HOME}/shaula-overlay"
  remove_path "${XDG_BIN_HOME}/shaula-preview"
  remove_path "${XDG_DATA_HOME}/applications/shaula.desktop"
  remove_path "${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg"
  remove_path "${SHAULA_GENERATED_DIR}/niri-shaula.kdl"
  log "kept ${SHAULA_CONFIG_DIR}/config.toml"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --version)
      shift
      [ "$#" -gt 0 ] || die "--version requires a tag"
      INSTALL_VERSION="$1"
      ;;
    --yes)
      ASSUME_YES=1
      ;;
    --no-integrations)
      INSTALL_INTEGRATIONS=0
      ;;
    --no-desktop)
      INSTALL_DESKTOP=0
      ;;
    --no-icon)
      INSTALL_ICON=0
      ;;
    --uninstall)
      UNINSTALL=1
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

if [ "$UNINSTALL" -eq 1 ]; then
  run_uninstall
else
  install_release
fi
