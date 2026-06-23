#!/usr/bin/env sh
set -eu

REPO_URL="https://github.com/fgonzalezurriola/shaula"
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
INSTALL_VERSION="${SHAULA_VERSION:-}"
ASSUME_YES=0
INSTALL_INTEGRATIONS=1
INSTALL_DESKTOP=1
INSTALL_ICON=1
INSTALL_NIRI_KEYBINDS=0
UNINSTALL=0
RELEASE_EXTRACT_DIR=""
INSTALL_CONTEXT="${SHAULA_INSTALL_CONTEXT:-release}"

XDG_BIN_HOME="${XDG_BIN_HOME:-${HOME}/.local/bin}"
XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${HOME}/.config}"
SHAULA_CONFIG_DIR="${XDG_CONFIG_HOME}/shaula"
SHAULA_GENERATED_DIR="${SHAULA_CONFIG_DIR}/generated"
NOCTALIA_CONFIG_DIR="${XDG_CONFIG_HOME}/noctalia"
NOCTALIA_PLUGINS_DIR="${NOCTALIA_CONFIG_DIR}/plugins"
NOCTALIA_PLUGIN_DIR="${NOCTALIA_PLUGINS_DIR}/shaula"
NOCTALIA_PLUGINS_JSON="${NOCTALIA_CONFIG_DIR}/plugins.json"
NOCTALIA_SETTINGS_JSON="${NOCTALIA_CONFIG_DIR}/settings.json"
NOCTALIA_MANAGED_MARKER=".shaula-managed"

usage() {
  cat <<'EOF'
Usage: scripts/install.sh [options]
       scripts/install.sh v0.1.2

Install Shaula for the current user. The installer only uses sudo when you
explicitly confirm Arch runtime dependency installation and never overwrites an
existing ~/.config/shaula/config.toml.

Options:
  --help              Show this help.
  --version <tag>     Install a specific GitHub release tag.
  --yes               Do not prompt before installing.
  --no-integrations   Skip setup integration prompts.
  --no-desktop        Skip the desktop entry.
  --no-icon           Skip the hicolor app icon.
  --niri-keybinds     Install recommended Shaula Niri keyboard shortcuts.
  --uninstall         Remove files installed by this script.

Environment:
  SHAULA_VERSION             Install a specific GitHub release tag.
  SHAULA_RELEASE_ASSET_URL   Override release archive URL.
  SHAULA_SHA256SUMS_URL      Override SHA256SUMS URL.
  SHAULA_INSTALL_ASSUME_ARCH Testing only: treat this host as Arch-like.
  SHAULA_INSTALL_TEST_MISSING_ARCH_PACKAGES
                             Testing only: simulate missing Arch packages.
EOF
}

color_enabled() {
  [ -z "${NO_COLOR:-}" ] && [ -t 1 ]
}

color_text() {
  code="$1"
  text="$2"
  if color_enabled; then
    printf '\033[%sm%s\033[0m' "$code" "$text"
  else
    printf '%s' "$text"
  fi
}

log() {
  printf '%s\n' "$*"
}

info() {
  printf '  %s\n' "$*"
}

section() {
  printf '\n%s\n' "$*"
}

ok() {
  if color_enabled; then
    printf '\033[32mok:\033[0m %s\n' "$*"
  else
    printf 'ok: %s\n' "$*"
  fi
}

warn() {
  if color_enabled; then
    printf '\033[33mwarning:\033[0m %s\n' "$*" >&2
  else
    printf 'warning: %s\n' "$*" >&2
  fi
}

die() {
  if color_enabled; then
    printf '\033[31merror:\033[0m %s\n' "$*" >&2
  else
    printf 'error: %s\n' "$*" >&2
  fi
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

can_prompt() {
  [ -r /dev/tty ] && [ -w /dev/tty ]
}

prompt_yes_no() {
  question="$1"
  if ! can_prompt; then
    return 1
  fi
  color_text "33" "$question" > /dev/tty
  if ! IFS= read -r answer < /dev/tty; then
    return 1
  fi
  case "$answer" in
    y|Y|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

confirm() {
  if [ "$ASSUME_YES" -eq 1 ]; then
    return 0
  fi
  if [ "$INSTALL_CONTEXT" = "release" ]; then
    return 0
  fi
  if [ "$INSTALL_CONTEXT" = "dev" ]; then
    prompt="Install this local dev build of Shaula into ${XDG_BIN_HOME}, ${XDG_DATA_HOME}, and ${SHAULA_CONFIG_DIR}? [y/N] "
  else
    prompt="Install Shaula into ${XDG_BIN_HOME}, ${XDG_DATA_HOME}, and ${SHAULA_CONFIG_DIR}? [y/N] "
  fi
  prompt_yes_no "$prompt" || die "installation cancelled"
}

confirm_noctalia_widget() {
  if [ "$ASSUME_YES" -eq 1 ]; then
    return 0
  fi
  log "Noctalia integration:"
  info "add the Shaula widget to your Noctalia bar"
  info "enable the Shaula plugin in Noctalia"
  info "create backups before changing Noctalia settings"
  if [ "$INSTALL_CONTEXT" = "dev" ]; then
    prompt='Install/reload this local Shaula Noctalia Bar Widget? [y/N] '
  else
    prompt='Install Shaula Noctalia Bar Widget? [y/N] '
  fi
  prompt_yes_no "$prompt"
}

confirm_niri_keybinds() {
  if [ "$INSTALL_NIRI_KEYBINDS" -eq 1 ]; then
    return 0
  fi
  if [ "$ASSUME_YES" -eq 1 ]; then
    return 1
  fi
  log "Niri keybinds to install:"
  info "Ctrl+Shift+1 -> choose an area and open the preview"
  info "Ctrl+Shift+2 -> capture the current screen and save it"
  info "Ctrl+Shift+3 -> capture all screens and save it"
  info "Ctrl+Shift+4 -> open Shaula settings"
  prompt='Install these Shaula Niri shortcuts? [y/N] '
  if prompt_yes_no "$prompt"; then
    INSTALL_NIRI_KEYBINDS=1
    return 0
  fi
  return 1
}

detect_arch() {
  machine="$(uname -m)"
  case "$machine" in
    x86_64|amd64) printf 'x86_64' ;;
    *) die "unsupported architecture: $machine (supported release asset: linux x86_64)" ;;
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
    if [ -t 1 ]; then
      curl -fL --progress-bar "$url" -o "$dest"
    else
      curl -fsSL "$url" -o "$dest"
    fi
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

install_dir_if_present() {
  source="$1"
  dest="$2"
  if [ -d "$source" ]; then
    mkdir -p "$dest"
    cp -R "$source"/. "$dest"/
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
Exec=${XDG_BIN_HOME}/shaula capture area --json
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
    "${XDG_CONFIG_HOME}/niri/config.kdl" \
    "${XDG_CONFIG_HOME}/niri/cfg" \
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
    "${NOCTALIA_CONFIG_DIR}" \
    "${NOCTALIA_PLUGINS_DIR}" \
    "${NOCTALIA_PLUGINS_JSON}" \
    "${NOCTALIA_SETTINGS_JSON}" \
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

noctalia_plugin_source_dir() {
  for path in \
    "${RELEASE_EXTRACT_DIR}/share/shaula/integrations/noctalia/shaula" \
    "${RELEASE_EXTRACT_DIR}/integrations/noctalia/shaula" \
    "${REPO_ROOT}/integrations/noctalia/shaula" \
    "${SCRIPT_DIR}/../integrations/noctalia/shaula"
  do
    if [ -f "${path}/manifest.json" ] && [ -f "${path}/BarWidget.qml" ]; then
      printf '%s' "$path"
      return 0
    fi
  done
  return 1
}

backup_file() {
  path="$1"
  suffix="$2"
  timestamp="$(date +%Y%m%d%H%M%S)"
  backup="${path}.shaula-backup-${timestamp}${suffix}"
  cp "$path" "$backup"
  log "backed up $path to $backup"
}

install_noctalia_plugin_files() {
  source_dir="$(noctalia_plugin_source_dir)" || {
    warn "Noctalia plugin source files were not found; skipped widget install."
    return 1
  }
  mkdir -p "$NOCTALIA_PLUGIN_DIR"
  for file in manifest.json BarWidget.qml README.md; do
    install -m 0644 "${source_dir}/${file}" "${NOCTALIA_PLUGIN_DIR}/${file}"
  done
  printf 'installed-by=shaula\n' > "${NOCTALIA_PLUGIN_DIR}/${NOCTALIA_MANAGED_MARKER}"
  log "installed ${NOCTALIA_PLUGIN_DIR}"
}

edit_noctalia_plugins_json_enable() {
  [ -f "$NOCTALIA_PLUGINS_JSON" ] || {
    warn "Noctalia plugins.json missing; copied plugin files only."
    return 1
  }
  backup_file "$NOCTALIA_PLUGINS_JSON" ""
  if ! python3 - "$NOCTALIA_PLUGINS_JSON" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
try:
    data = json.loads(path.read_text(encoding="utf-8"))
except Exception as exc:
    raise SystemExit(f"invalid JSON before edit: {exc}")
if not isinstance(data, dict) or data.get("version") != 2 or not isinstance(data.get("states"), dict):
    raise SystemExit("unsupported Noctalia plugins.json structure")
states = data.setdefault("states", {})
state = states.get("shaula")
if not isinstance(state, dict):
    state = {}
state["enabled"] = True
state.setdefault("sourceUrl", "local")
states["shaula"] = state
path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
json.loads(path.read_text(encoding="utf-8"))
PY
  then
    latest_backup="$(ls -t "${NOCTALIA_PLUGINS_JSON}".shaula-backup-* 2>/dev/null | head -n 1)"
    [ -n "$latest_backup" ] && cp "$latest_backup" "$NOCTALIA_PLUGINS_JSON"
    warn "failed to edit Noctalia plugins.json; restored backup and left plugin files installed."
    return 1
  fi
  log "enabled Shaula in $NOCTALIA_PLUGINS_JSON"
}

edit_noctalia_settings_json_add_widget() {
  [ -f "$NOCTALIA_SETTINGS_JSON" ] || {
    warn "Noctalia settings.json missing; enable plugin manually in Noctalia settings."
    return 1
  }
  backup_file "$NOCTALIA_SETTINGS_JSON" ""
  if ! python3 - "$NOCTALIA_SETTINGS_JSON" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
try:
    data = json.loads(path.read_text(encoding="utf-8"))
except Exception as exc:
    raise SystemExit(f"invalid JSON before edit: {exc}")
if not isinstance(data, dict):
    raise SystemExit("unsupported Noctalia settings.json structure")
bar = data.get("bar")
if not isinstance(bar, dict):
    raise SystemExit("missing bar object")
widgets = bar.get("widgets")
if not isinstance(widgets, dict):
    raise SystemExit("missing bar.widgets object")
for section in ("left", "center", "right"):
    if not isinstance(widgets.get(section), list):
        widgets[section] = []
widget_id = "plugin:shaula"
for section in ("left", "center", "right"):
    for item in widgets[section]:
        if isinstance(item, dict) and item.get("id") == widget_id:
            path.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
            json.loads(path.read_text(encoding="utf-8"))
            raise SystemExit(0)
widgets["right"].append({"id": widget_id})
path.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
json.loads(path.read_text(encoding="utf-8"))
PY
  then
    latest_backup="$(ls -t "${NOCTALIA_SETTINGS_JSON}".shaula-backup-* 2>/dev/null | head -n 1)"
    [ -n "$latest_backup" ] && cp "$latest_backup" "$NOCTALIA_SETTINGS_JSON"
    warn "failed to edit Noctalia settings.json; restored backup. Add plugin:shaula manually to a bar section."
    return 1
  fi
  log "added plugin:shaula to $NOCTALIA_SETTINGS_JSON"
}

install_noctalia_widget() {
  install_noctalia_plugin_files || return 1
  plugins_edited=0
  settings_edited=0
  if edit_noctalia_plugins_json_enable; then
    plugins_edited=1
  fi
  if edit_noctalia_settings_json_add_widget; then
    settings_edited=1
  fi
  if [ "$plugins_edited" -eq 0 ] || [ "$settings_edited" -eq 0 ]; then
    log "Manual Noctalia enable: plugin files are in ${NOCTALIA_PLUGIN_DIR}; enable shaula and add plugin:shaula to the bar in Noctalia settings."
  fi
  return 0
}

edit_noctalia_plugins_json_disable() {
  [ -f "$NOCTALIA_PLUGINS_JSON" ] || return 1
  if ! python3 - "$NOCTALIA_PLUGINS_JSON" >/dev/null <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
if not isinstance(data, dict) or not isinstance(data.get("states"), dict):
    raise SystemExit(1)
raise SystemExit(0)
PY
  then
    warn "Noctalia plugins.json structure is unclear; remove shaula from states manually if needed."
    return 1
  fi
  backup_file "$NOCTALIA_PLUGINS_JSON" ""
  if ! python3 - "$NOCTALIA_PLUGINS_JSON" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
states = data.get("states")
if not isinstance(states, dict):
    raise SystemExit("unsupported Noctalia plugins.json structure")
states.pop("shaula", None)
path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
json.loads(path.read_text(encoding="utf-8"))
PY
  then
    latest_backup="$(ls -t "${NOCTALIA_PLUGINS_JSON}".shaula-backup-* 2>/dev/null | head -n 1)"
    [ -n "$latest_backup" ] && cp "$latest_backup" "$NOCTALIA_PLUGINS_JSON"
    warn "failed to remove Shaula from plugins.json; restored backup."
    return 1
  fi
  log "removed Shaula state from $NOCTALIA_PLUGINS_JSON"
}

edit_noctalia_settings_json_remove_widget() {
  [ -f "$NOCTALIA_SETTINGS_JSON" ] || return 1
  backup_file "$NOCTALIA_SETTINGS_JSON" ""
  if ! python3 - "$NOCTALIA_SETTINGS_JSON" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
widgets = data.get("bar", {}).get("widgets")
if not isinstance(widgets, dict):
    raise SystemExit("unsupported Noctalia settings.json structure")
for section in ("left", "center", "right"):
    if isinstance(widgets.get(section), list):
        widgets[section] = [
            item for item in widgets[section]
            if not (isinstance(item, dict) and item.get("id") == "plugin:shaula")
        ]
path.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
json.loads(path.read_text(encoding="utf-8"))
PY
  then
    latest_backup="$(ls -t "${NOCTALIA_SETTINGS_JSON}".shaula-backup-* 2>/dev/null | head -n 1)"
    [ -n "$latest_backup" ] && cp "$latest_backup" "$NOCTALIA_SETTINGS_JSON"
    warn "failed to remove plugin:shaula from settings.json; restored backup."
    return 1
  fi
  log "removed plugin:shaula from $NOCTALIA_SETTINGS_JSON"
}

uninstall_noctalia_widget() {
  if [ -f "${NOCTALIA_PLUGIN_DIR}/${NOCTALIA_MANAGED_MARKER}" ]; then
    edit_noctalia_plugins_json_disable || true
    edit_noctalia_settings_json_remove_widget || true
    rm -rf "$NOCTALIA_PLUGIN_DIR"
    log "removed $NOCTALIA_PLUGIN_DIR"
  elif [ -d "$NOCTALIA_PLUGIN_DIR" ]; then
    warn "kept $NOCTALIA_PLUGIN_DIR because it is not marked as installed by Shaula."
    warn "Remove it manually if it belongs to this Shaula install."
  fi
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
// Include or copy this manually from your active Niri config if desired.
// Detected Niri config candidate: ${detected_path}

window-rule {
    match app-id="^dev\\.shaula\\.preview$"
    open-floating true
}

binds {
    Mod+Shift+S { spawn "${shaula_path}" "capture" "area" "--json"; }
    Mod+Shift+F { spawn "${shaula_path}" "capture" "fullscreen" "--json" "--save"; }
    Mod+Shift+W { spawn "${shaula_path}" "capture" "all-screens" "--json" "--save"; }
}
EOF
  log "generated $dest"
}

install_niri_keybinds() {
  shaula_bin="${XDG_BIN_HOME}/shaula"
  if [ ! -x "$shaula_bin" ]; then
    warn "shaula binary not found at $shaula_bin; skipping keybinds install."
    return 1
  fi
  output="$("$shaula_bin" config niri-keybinds-install --json 2>&1)" || {
    if printf '%s' "$output" | grep -q "ERR_NIRI_KEYBIND_CONFLICT"; then
      warn "Niri keybind conflicts detected. Run with --force or resolve manually:"
      warn "  $shaula_bin config niri-keybinds-install --json --force"
      printf '%s\n' "$output" >&2
    else
      warn "Niri keybinds install failed: $output"
    fi
    return 1
  }
  ok "installed Niri keybinds:"
  info "Ctrl+Shift+1 -> choose an area and open the preview"
  info "Ctrl+Shift+2 -> capture the current screen and save it"
  info "Ctrl+Shift+3 -> capture all screens and save it"
  info "Ctrl+Shift+4 -> open Shaula settings"
}

uninstall_niri_keybinds() {
  if niri_path="$(detect_niri_config)"; then
    begin_marker="// BEGIN SHAULA MANAGED KEYBINDS"
    end_marker="// END SHAULA MANAGED KEYBINDS"
    if grep -q "$begin_marker" "$niri_path" 2>/dev/null; then
      backup_file "$niri_path" ""
      tmp_file="${niri_path}.shaula-tmp"
      awk -v begin="$begin_marker" -v end="$end_marker" '
        $0 ~ begin { skip=1; next }
        $0 ~ end { skip=0; next }
        !skip { print }
      ' "$niri_path" > "$tmp_file"
      mv "$tmp_file" "$niri_path"
      log "removed Shaula keybinds block from $niri_path"
    fi
  fi
}

is_arch_like_with_pacman() {
  command -v pacman >/dev/null 2>&1 || return 1
  if [ "${SHAULA_INSTALL_ASSUME_ARCH:-0}" = "1" ]; then
    return 0
  fi
  if [ -r /etc/os-release ]; then
    os_release="$(
      (
        . /etc/os-release
        printf '%s %s' "${ID:-}" "${ID_LIKE:-}"
      ) 2>/dev/null || true
    )"
    os_release="$(printf '%s' "$os_release" | tr '[:upper:]' '[:lower:]')"
    case " $os_release " in
      *" arch "*|*" cachyos "*|*" endeavouros "*|*" manjaro "*) return 0 ;;
    esac
    return 1
  fi
  return 0
}

missing_arch_runtime_packages() {
  if [ -n "${SHAULA_INSTALL_TEST_MISSING_ARCH_PACKAGES:-}" ]; then
    printf '%s' "$SHAULA_INSTALL_TEST_MISSING_ARCH_PACKAGES"
    return 0
  fi
  missing=""
  for package in grim wl-clipboard gtk4 gtk4-layer-shell xdg-desktop-portal xdg-desktop-portal-gtk; do
    if ! pacman -Q "$package" >/dev/null 2>&1; then
      missing="${missing} ${package}"
    fi
  done
  printf '%s' "${missing# }"
}

install_arch_runtime_deps_if_confirmed() {
  if ! is_arch_like_with_pacman; then
    log "Arch runtime dependency helper skipped; pacman-based distro not detected."
    return 0
  fi
  section "Checking system dependencies"
  missing_packages="$(missing_arch_runtime_packages)"
  if [ -z "$missing_packages" ]; then
    ok "Arch runtime packages already installed."
    return 0
  fi
  if [ "$ASSUME_YES" -eq 1 ]; then
    warn "missing Arch runtime packages: $missing_packages"
    warn "rerun without --yes to confirm sudo pacman -S --needed ${missing_packages}"
    return 0
  fi
  if ! can_prompt; then
    warn "missing Arch runtime packages: $missing_packages"
    warn "install them manually or rerun with a TTY to confirm sudo pacman -S --needed ${missing_packages}"
    return 0
  fi
  # `curl | sh` consumes stdin, so installer prompts must use `/dev/tty`.
  if ! prompt_yes_no "Missing Arch runtime packages for Shaula: ${missing_packages}. Install now with sudo pacman -S --needed ${missing_packages}? [y/N] "; then
    warn "skipped Arch runtime package install: $missing_packages"
    return 0
  fi
  if ! command -v sudo >/dev/null 2>&1; then
    warn "cannot install Arch runtime packages automatically because sudo is not installed"
    return 0
  fi
  if sudo pacman -S --needed $missing_packages; then
    ok "installed Arch runtime packages: $missing_packages"
  else
    warn "Arch runtime package install failed or was cancelled; continuing with Shaula install."
  fi
}

run_shaula_setup() {
  shaula_bin="${XDG_BIN_HOME}/shaula"
  if [ ! -x "$shaula_bin" ]; then
    warn "shaula binary not found at $shaula_bin; skipped setup."
    return 0
  fi

  setup_args=""
  if [ "$ASSUME_YES" -eq 1 ]; then
    setup_args="${setup_args} --yes"
    if [ "$INSTALL_NIRI_KEYBINDS" -eq 0 ]; then
      setup_args="${setup_args} --skip-niri-keybinds"
    fi
  fi
  if [ "$INSTALL_INTEGRATIONS" -eq 0 ]; then
    setup_args="${setup_args} --no-integrations"
  fi
  if [ "$INSTALL_NIRI_KEYBINDS" -eq 1 ]; then
    setup_args="${setup_args} --niri-keybinds"
  fi

  noctalia_source="$(noctalia_plugin_source_dir 2>/dev/null || true)"
  if [ -n "$noctalia_source" ]; then
    SHAULA_NOCTALIA_PLUGIN_SOURCE="$noctalia_source" "$shaula_bin" setup $setup_args || warn "shaula setup failed; run 'shaula setup' manually."
  else
    "$shaula_bin" setup $setup_args || warn "shaula setup failed; run 'shaula setup' manually."
  fi
}

warn_runtime_tools() {
  for tool in grim wl-copy wl-paste gdbus niri quickshell; do
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

  install_arch_runtime_deps_if_confirmed

  section "Downloading Shaula"
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
  RELEASE_EXTRACT_DIR="$extract_dir"
  mkdir -p "$extract_dir"
  tar -xzf "$archive" -C "$extract_dir"

  shaula_bin="$(find_file "$extract_dir" shaula)"
  [ -n "$shaula_bin" ] || die "release archive does not contain shaula"

  confirm
  section "Installing files"
  mkdir -p "$XDG_BIN_HOME" "$SHAULA_GENERATED_DIR"
  install -m 0755 "$shaula_bin" "${XDG_BIN_HOME}/shaula"
  log "installed ${XDG_BIN_HOME}/shaula"
  install_file_if_present "$(find_file "$extract_dir" shaula-overlay)" "${XDG_BIN_HOME}/shaula-overlay" 0755
  install_file_if_present "$(find_file "$extract_dir" shaula-preview)" "${XDG_BIN_HOME}/shaula-preview" 0755
  install_file_if_present "$(find_file "$extract_dir" shaula-crop-image)" "${XDG_BIN_HOME}/shaula-crop-image" 0755
  install_file_if_present "$(find_file "$extract_dir" shaula-portal-screenshot)" "${XDG_BIN_HOME}/shaula-portal-screenshot" 0755
  install_file_if_present "$(find_file "$extract_dir" shaula-settings)" "${XDG_BIN_HOME}/shaula-settings" 0755

  if [ "$INSTALL_DESKTOP" -eq 1 ]; then
    write_desktop_file
  fi
  if [ "$INSTALL_ICON" -eq 1 ]; then
    bundled_icon_theme="$(find "$extract_dir" -type d -path '*/icons/hicolor' | head -n 1)"
    if [ -n "$bundled_icon_theme" ]; then
      install_dir_if_present "$bundled_icon_theme" "${XDG_DATA_HOME}/icons/hicolor"
    fi
    bundled_icon="$(find "$extract_dir" -type f -path '*/icons/hicolor/scalable/apps/shaula.svg' | head -n 1)"
    bundled_png_icon="$(find "$extract_dir" -type f -path '*/icons/hicolor/*/apps/shaula.png' | head -n 1)"
    if [ -n "$bundled_icon" ]; then
      install_file_if_present "$bundled_icon" "${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg" 0644
    elif [ -n "$bundled_png_icon" ]; then
      log "installed bundled PNG app icons"
    else
      write_icon_file
    fi
  fi

  warn_runtime_tools

  section "Running user setup"
  run_shaula_setup

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
  uninstall_niri_keybinds
  remove_path "${XDG_BIN_HOME}/shaula"
  remove_path "${XDG_BIN_HOME}/shaula-overlay"
  remove_path "${XDG_BIN_HOME}/shaula-preview"
  remove_path "${XDG_BIN_HOME}/shaula-crop-image"
  remove_path "${XDG_BIN_HOME}/shaula-portal-screenshot"
  remove_path "${XDG_BIN_HOME}/shaula-settings"
  remove_path "${XDG_DATA_HOME}/applications/shaula.desktop"
  remove_path "${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg"
  for size in 48x48 64x64 128x128 256x256 512x512; do
    remove_path "${XDG_DATA_HOME}/icons/hicolor/${size}/apps/shaula.png"
  done
  remove_path "${SHAULA_GENERATED_DIR}/niri-shaula.kdl"
  uninstall_noctalia_widget
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
    --niri-keybinds)
      INSTALL_NIRI_KEYBINDS=1
      ;;
    --uninstall)
      UNINSTALL=1
      ;;
    v*)
      [ -z "$INSTALL_VERSION" ] || die "version specified more than once"
      INSTALL_VERSION="$1"
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
