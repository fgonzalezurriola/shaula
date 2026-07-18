#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="${1:?source root is required}"
ARCHIVE="${2:?archive path is required}"
ARCH="${3:?architecture is required}"

case "${ARCH}" in
  x86_64) expected_machine='Advanced Micro Devices X86-64' ;;
  aarch64) expected_machine='AArch64' ;;
  *) echo "unsupported release architecture: ${ARCH}" >&2; exit 2 ;;
esac

expected_name="shaula-linux-${ARCH}.tar.gz"
[[ $(basename "${ARCHIVE}") == "${expected_name}" ]] || {
  echo "release archive has wrong name: ${ARCHIVE}" >&2
  exit 1
}
[[ -f "${ARCHIVE}" ]] || {
  echo "missing release archive: ${ARCHIVE}" >&2
  exit 1
}

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
mkdir -p "${TMP_DIR}/extract"
tar -xzf "${ARCHIVE}" -C "${TMP_DIR}/extract"

manifest="${TMP_DIR}/extract/share/shaula/release-manifest.txt"
[[ -f "${manifest}" ]] || {
  echo "release archive is missing share/shaula/release-manifest.txt" >&2
  exit 1
}
[[ ! -e "${TMP_DIR}/extract/share/icons/hicolor/index.theme" ]] || {
  echo "release archive must not contain share/icons/hicolor/index.theme" >&2
  exit 1
}
if find "${TMP_DIR}/extract" -type l -print -quit | grep -q .; then
  echo "release archive contains an unexpected symlink" >&2
  exit 1
fi

while IFS= read -r relative || [[ -n ${relative} ]]; do
  [[ -n ${relative} && ${relative} != \#* ]] || continue
  case "${relative}" in
    /*|../*|*/../*|*/..)
      echo "invalid release manifest path: ${relative}" >&2
      exit 1
      ;;
  esac
  path="${TMP_DIR}/extract/${relative}"
  [[ -f "${path}" ]] || {
    echo "release manifest path is missing: ${relative}" >&2
    exit 1
  }
  if [[ ${relative} == bin/* ]]; then
    [[ -x "${path}" ]] || {
      echo "release binary is not executable: ${relative}" >&2
      exit 1
    }
    machine="$(readelf -h "${path}" | awk -F: '/Machine:/ { sub(/^[[:space:]]+/, "", $2); print $2; exit }')"
    [[ ${machine} == "${expected_machine}" ]] || {
      echo "wrong binary architecture for ${relative}: ${machine}" >&2
      exit 1
    }
  fi
done < "${manifest}"

{
  sed '/^[[:space:]]*$/d; /^[[:space:]]*#/d' "${manifest}"
  printf '%s\n' 'share/shaula/release-manifest.txt'
} | LC_ALL=C sort -u > "${TMP_DIR}/expected-files"

tar -tzf "${ARCHIVE}" \
  | sed 's#^\./##' \
  | sed '/\/$/d; /^[[:space:]]*$/d' \
  | LC_ALL=C sort -u > "${TMP_DIR}/archive-files"

if ! cmp -s "${TMP_DIR}/expected-files" "${TMP_DIR}/archive-files"; then
  echo "release archive files differ from the manifest-backed payload" >&2
  diff -u "${TMP_DIR}/expected-files" "${TMP_DIR}/archive-files" >&2 || true
  exit 1
fi

top_level="$(awk -F/ 'NF { print $1 }' "${TMP_DIR}/archive-files" \
  | LC_ALL=C sort -u | tr '\n' ' ')"
[[ ${top_level} == 'bin share ' ]] || {
  echo "unexpected release archive top-level paths: ${top_level}" >&2
  exit 1
}

printf 'ok release archive arch=%s path=%s\n' "${ARCH}" "${ARCHIVE}"
