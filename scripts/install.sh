#!/usr/bin/env sh
set -eu

REPO_URL="https://github.com/fgonzalezurriola/shaula"
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
INSTALL_VERSION=""
ASSUME_YES=0
INSTALL_INTEGRATIONS=1
INSTALL_DESKTOP=1
INSTALL_ICON=1
UNINSTALL=0
RELEASE_EXTRACT_DIR=""

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

confirm_noctalia_widget() {
  if [ "$ASSUME_YES" -eq 1 ]; then
    return 0
  fi
  printf 'Detected Noctalia Shell. Install Shaula Noctalia Bar Widget? [y/N] '
  read answer
  case "$answer" in
    y|Y|yes|YES) return 0 ;;
    *) return 1 ;;
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
    Mod+Shift+F { spawn "${shaula_path}" "capture" "fullscreen" "--json"; }
    Mod+Shift+W { spawn "${shaula_path}" "capture" "all-screens" "--json"; }
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
  RELEASE_EXTRACT_DIR="$extract_dir"
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
  install_file_if_present "$(find_file "$extract_dir" shaula-settings)" "${XDG_BIN_HOME}/shaula-settings" 0755

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
      if confirm_noctalia_widget; then
        install_noctalia_widget
      else
        log "skipped Noctalia Bar Widget install."
      fi
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
  remove_path "${XDG_BIN_HOME}/shaula-settings"
  remove_path "${XDG_DATA_HOME}/applications/shaula.desktop"
  remove_path "${XDG_DATA_HOME}/icons/hicolor/scalable/apps/shaula.svg"
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
