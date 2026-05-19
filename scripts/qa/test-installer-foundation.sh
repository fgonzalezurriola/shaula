#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT_DIR}"

if ! command -v jq >/dev/null 2>&1; then
  echo "ERR_QA_TOOL_MISSING tool=jq" >&2
  exit 1
fi

zig build

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

release_dir="${tmp_dir}/release"
home_dir="${tmp_dir}/home"
xdg_bin="${tmp_dir}/xdg-bin"
xdg_data="${tmp_dir}/xdg-data"
xdg_config="${tmp_dir}/xdg-config"
case "$(uname -m)" in
  x86_64|amd64) release_arch="x86_64" ;;
  aarch64|arm64) release_arch="aarch64" ;;
  *)
    echo "ERR_INSTALLER_QA_UNSUPPORTED_ARCH arch=$(uname -m)" >&2
    exit 1
    ;;
esac
mkdir -p "${release_dir}/bin" "${home_dir}/.config/niri" "${xdg_bin}" "${xdg_data}" "${xdg_config}/shaula"

install -m 0755 zig-out/bin/shaula "${release_dir}/bin/shaula"
if [[ -x zig-out/bin/shaula-overlay ]]; then
  install -m 0755 zig-out/bin/shaula-overlay "${release_dir}/bin/shaula-overlay"
fi
if [[ -x zig-out/bin/shaula-preview ]]; then
  install -m 0755 zig-out/bin/shaula-preview "${release_dir}/bin/shaula-preview"
fi
if [[ -x zig-out/bin/shaula-crop-image ]]; then
  install -m 0755 zig-out/bin/shaula-crop-image "${release_dir}/bin/shaula-crop-image"
fi

archive="${tmp_dir}/shaula-linux-${release_arch}.tar.gz"
tar -C "${release_dir}" -czf "${archive}" .
(cd "${tmp_dir}" && sha256sum "./shaula-linux-${release_arch}.tar.gz" > SHA256SUMS)

printf 'niri config\n' > "${home_dir}/.config/niri/config.kdl"
printf 'sentinel = true\n' > "${xdg_config}/shaula/config.toml"

common_env=(
  "HOME=${home_dir}"
  "XDG_BIN_HOME=${xdg_bin}"
  "XDG_DATA_HOME=${xdg_data}"
  "XDG_CONFIG_HOME=${xdg_config}"
  "SHAULA_RELEASE_ASSET_URL=file://${archive}"
  "SHAULA_SHA256SUMS_URL=file://${tmp_dir}/SHA256SUMS"
)

env "${common_env[@]}" scripts/install.sh >/tmp/shaula-installer-install.out
test -x "${xdg_bin}/shaula"
test -f "${xdg_data}/applications/shaula.desktop"
test -f "${xdg_data}/icons/hicolor/scalable/apps/shaula.svg"
grep -q 'sentinel = true' "${xdg_config}/shaula/config.toml"
test -f "${xdg_config}/shaula/generated/niri-shaula.kdl"
grep -q "Mod+Shift+S" "${xdg_config}/shaula/generated/niri-shaula.kdl"
grep -q "${xdg_bin}/shaula" "${xdg_config}/shaula/generated/niri-shaula.kdl"

env HOME="${home_dir}" XDG_BIN_HOME="${xdg_bin}" XDG_DATA_HOME="${xdg_data}" XDG_CONFIG_HOME="${xdg_config}" "${xdg_bin}/shaula" doctor >/tmp/shaula-installer-doctor-human.out
env HOME="${home_dir}" XDG_BIN_HOME="${xdg_bin}" XDG_DATA_HOME="${xdg_data}" XDG_CONFIG_HOME="${xdg_config}" "${xdg_bin}/shaula" doctor --json \
  | jq -e '.ok==true and .paths.config_exists==true and .paths.niri_snippet_exists==true and (.tools["wl-copy"].found|type=="boolean")' >/dev/null

env "${common_env[@]}" scripts/install.sh --yes >/tmp/shaula-installer-reinstall.out
grep -q 'sentinel = true' "${xdg_config}/shaula/config.toml"

env HOME="${home_dir}" XDG_BIN_HOME="${xdg_bin}" XDG_DATA_HOME="${xdg_data}" XDG_CONFIG_HOME="${xdg_config}" scripts/install.sh --uninstall >/tmp/shaula-installer-uninstall.out
test ! -e "${xdg_bin}/shaula"
test -f "${xdg_config}/shaula/config.toml"
grep -q 'sentinel = true' "${xdg_config}/shaula/config.toml"

grep -q 'curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh$' README.md
grep -q 'curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --uninstall' README.md
grep -q 'curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --yes' README.md
grep -q 'curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --version v1.0.0' README.md

echo "Installer foundation QA passed."
