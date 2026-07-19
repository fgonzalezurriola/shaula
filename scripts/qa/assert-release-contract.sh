#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="${1:?source root is required}"
DIST_DIR="${2:-}"
TAG_NAME="${3:-}"

version="$(sed -n "s/^[[:space:]]*version: '\([^']*\)'.*/\1/p" \
  "${SOURCE_ROOT}/meson.build" | head -n 1)"
[[ -n ${version} ]] || {
  echo 'could not read the Meson project version' >&2
  exit 1
}
expected_tag="v${version}"
if [[ -n ${TAG_NAME} && ${TAG_NAME} != "${expected_tag}" ]]; then
  echo "release tag ${TAG_NAME} does not match Meson version ${version}" >&2
  exit 1
fi

notes="${SOURCE_ROOT}/docs/release-${expected_tag}.md"
[[ -f ${notes} ]] || {
  echo "missing release notes: docs/release-${expected_tag}.md" >&2
  exit 1
}
grep -Fqx "# Shaula ${expected_tag}" "${notes}" || {
  echo "release notes heading does not match ${expected_tag}" >&2
  exit 1
}

grep -Fq "version: '${version}'" "${SOURCE_ROOT}/meson.build"
grep -Fq "latest stable release is **${expected_tag}**" \
  "${SOURCE_ROOT}/README.md"
grep -Fq "scripts/install.sh ${expected_tag}" \
  "${SOURCE_ROOT}/scripts/install.sh"
jq -e --arg version "${version}" '.version == $version' \
  "${SOURCE_ROOT}/integrations/noctalia/shaula/manifest.json" >/dev/null

for package in shaula shaula-bin; do
  package_root="${SOURCE_ROOT}/aur/${package}"
  grep -Fqx "pkgver=${version}" "${package_root}/PKGBUILD"
  grep -Fq $'\tpkgver = '"${version}" "${package_root}/.SRCINFO"
  if command -v makepkg >/dev/null 2>&1; then
    generated="$(mktemp)"
    (cd "${package_root}" && makepkg --printsrcinfo) > "${generated}"
    if ! cmp -s "${generated}" "${package_root}/.SRCINFO"; then
      echo "${package}/.SRCINFO is stale" >&2
      diff -u "${package_root}/.SRCINFO" "${generated}" >&2 || true
      rm -f "${generated}"
      exit 1
    fi
    rm -f "${generated}"
  fi
done

grep -Fqx "arch=('x86_64' 'aarch64')" \
  "${SOURCE_ROOT}/aur/shaula/PKGBUILD"
grep -Fqx "arch=('x86_64' 'aarch64')" \
  "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
grep -Fq "shaula-linux-x86_64.tar.gz" \
  "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
grep -Fq "shaula-linux-aarch64.tar.gz" \
  "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
[[ $(grep -c $'\tarch = x86_64' "${SOURCE_ROOT}/aur/shaula-bin/.SRCINFO") -eq 1 ]]
[[ $(grep -c $'\tarch = aarch64' "${SOURCE_ROOT}/aur/shaula-bin/.SRCINFO") -eq 1 ]]

# Checked-in PKGBUILDs are release templates: their SKIP markers are required so
# preparation never guesses hashes for remote artifacts. release.yml replaces
# them only in writable AUR clones after publishing the GitHub assets.
grep -Fq "sha256sums=('SKIP')" "${SOURCE_ROOT}/aur/shaula/PKGBUILD"
grep -Fq "sha256sums=('SKIP')" "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
grep -Fq "sha256sums_x86_64=('SKIP')" \
  "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
grep -Fq "sha256sums_aarch64=('SKIP')" \
  "${SOURCE_ROOT}/aur/shaula-bin/PKGBUILD"
if grep -REq '598c04b65ec31f44925db1b339c94638d171344cbe4a8ba17d2b76530f71a277|9bd5f0f2110b8cf028ad388ffee8985c9ad5f849c65169019a39a1fa6cf631cf' \
    "${SOURCE_ROOT}/aur"; then
  echo 'stale v0.1.5 AUR checksum remains' >&2
  exit 1
fi
grep -Fq 'checked-in PKGBUILDs must retain every `SKIP` marker' \
  "${SOURCE_ROOT}/docs/releasing.md"
grep -Fq 'AUR_SSH_KEY' "${SOURCE_ROOT}/docs/releasing.md"

if [[ -n ${DIST_DIR} ]]; then
  x86_archive="${DIST_DIR}/shaula-linux-x86_64.tar.gz"
  arm_archive="${DIST_DIR}/shaula-linux-aarch64.tar.gz"
  sums="${DIST_DIR}/SHA256SUMS"
  [[ -f ${x86_archive} && -f ${arm_archive} && -f ${sums} ]] || {
    echo 'release directory must contain both archives and SHA256SUMS' >&2
    exit 1
  }
  "${SOURCE_ROOT}/scripts/qa/assert-release-archive.sh" \
    "${SOURCE_ROOT}" "${x86_archive}" x86_64
  "${SOURCE_ROOT}/scripts/qa/assert-release-archive.sh" \
    "${SOURCE_ROOT}" "${arm_archive}" aarch64
  (
    cd "${DIST_DIR}"
    sha256sum -c SHA256SUMS
  )
  [[ $(awk 'NF == 2 { count += 1 } END { print count + 0 }' "${sums}") -eq 2 ]]
  grep -Eq '[ *]shaula-linux-x86_64\.tar\.gz$' "${sums}"
  grep -Eq '[ *]shaula-linux-aarch64\.tar\.gz$' "${sums}"
fi

printf 'ok release contract version=%s tag=%s\n' "${version}" "${expected_tag}"
