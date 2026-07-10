#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

REAL_PATH="${PATH}"
export SHAULA_TEST_QS_STATE="${TMP_DIR}/qs-state"
export SHAULA_TEST_QS_ARGS="${TMP_DIR}/qs-args"
export SHAULA_TEST_SYSTEMD_ENV="${TMP_DIR}/systemd-env"
export SHAULA_NOCTALIA_LOG_PATH="${TMP_DIR}/noctalia.log"
export SHAULA_NOCTALIA_READY_ATTEMPTS=20
export SHAULA_NOCTALIA_STOP_ATTEMPTS=5
export SHAULA_NOCTALIA_POLL_SECONDS=0.01

cat >"${TMP_DIR}/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "$*" in
  "--user show-environment")
    cat "${SHAULA_TEST_SYSTEMD_ENV}"
    ;;
  "--user restart noctalia.service")
    if [[ "${SHAULA_TEST_SYSTEMD_RESTART:-fail}" != "success" ]]; then
      exit 1
    fi
    case "${SHAULA_TEST_SYSTEMD_INSTANCE:-replace}" in
      replace)
        printf '%s' service-new >"${SHAULA_TEST_QS_STATE}"
        ;;
      keep)
        ;;
      none)
        : >"${SHAULA_TEST_QS_STATE}"
        ;;
      *)
        exit 2
        ;;
    esac
    ;;
  "--user is-active --quiet noctalia.service")
    [[ "${SHAULA_TEST_SYSTEMD_ACTIVE:-0}" == "1" ]]
    ;;
  *)
    exit 1
    ;;
esac
EOF
chmod +x "${TMP_DIR}/systemctl"

cat >"${TMP_DIR}/qs" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}" in
  list)
    if [[ -s "${SHAULA_TEST_QS_STATE}" ]]; then
      id="$(cat "${SHAULA_TEST_QS_STATE}")"
      cat <<OUT
Instance ${id}:
  Process ID: 4242
  Shell ID: shell-${id}
  Config path: /tmp/quickshell/noctalia-shell/shell.qml
  Display connection: wayland,wayland-test
  Launch time: 2026-07-10 12:00:00
OUT
    fi
    ;;
  kill)
    if [[ "${SHAULA_TEST_QS_STICKY:-0}" != "1" ]]; then
      : >"${SHAULA_TEST_QS_STATE}"
    fi
    ;;
  -d)
    printf '%s\n' "$@" >"${SHAULA_TEST_QS_ARGS}"
    printf '%s' launcher-new >"${SHAULA_TEST_QS_STATE}"
    ;;
  *)
    exit 2
    ;;
esac
EOF
chmod +x "${TMP_DIR}/qs"

export PATH="${TMP_DIR}:${REAL_PATH}"
export SHAULA_DEV_SOURCE_ONLY=1
# shellcheck source=../../dev
source "${ROOT_DIR}/dev"
unset SHAULA_DEV_SOURCE_ONLY

assert_equal() {
  local expected="${1}"
  local actual="${2}"
  local label="${3}"
  if [[ "${actual}" != "${expected}" ]]; then
    printf 'expected %s=%q, got %q\n' "${label}" "${expected}" "${actual}" >&2
    exit 1
  fi
}

cat >"${SHAULA_TEST_SYSTEMD_ENV}" <<'EOF'
WAYLAND_DISPLAY=wayland-systemd
DISPLAY=:77
XDG_RUNTIME_DIR=/run/user/777
DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/777/bus
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=niri
NIRI_SOCKET=/run/user/777/niri.sock
IGNORED_SECRET=do-not-import
EOF
unset WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS
unset XDG_SESSION_TYPE XDG_CURRENT_DESKTOP NIRI_SOCKET IGNORED_SECRET
export DISPLAY=:preserved
import_graphical_session_environment
assert_equal wayland-systemd "${WAYLAND_DISPLAY}" WAYLAND_DISPLAY
assert_equal :preserved "${DISPLAY}" DISPLAY
assert_equal /run/user/777 "${XDG_RUNTIME_DIR}" XDG_RUNTIME_DIR
assert_equal unix:path=/run/user/777/bus "${DBUS_SESSION_BUS_ADDRESS}" DBUS_SESSION_BUS_ADDRESS
assert_equal wayland "${XDG_SESSION_TYPE}" XDG_SESSION_TYPE
assert_equal niri "${XDG_CURRENT_DESKTOP}" XDG_CURRENT_DESKTOP
assert_equal /run/user/777/niri.sock "${NIRI_SOCKET}" NIRI_SOCKET
if [[ -n "${IGNORED_SECRET:-}" ]]; then
  echo "unexpected import of non-allowlisted variable" >&2
  exit 1
fi

printf '%s' old-instance >"${SHAULA_TEST_QS_STATE}"
export SHAULA_TEST_QS_STICKY=0
restart_noctalia_with_launcher qs >/dev/null
assert_equal launcher-new "$(cat "${SHAULA_TEST_QS_STATE}")" launcher-instance
mapfile -t launch_args <"${SHAULA_TEST_QS_ARGS}"
assert_equal -d "${launch_args[0]}" launcher-arg-0
assert_equal -c "${launch_args[1]}" launcher-arg-1
assert_equal noctalia-shell "${launch_args[2]}" launcher-arg-2
if grep -Fq -- '--allow-duplicate' "${SHAULA_TEST_QS_ARGS}"; then
  echo "launcher unexpectedly permits duplicates" >&2
  exit 1
fi

printf '%s' stuck-instance >"${SHAULA_TEST_QS_STATE}"
rm -f "${SHAULA_TEST_QS_ARGS}"
export SHAULA_TEST_QS_STICKY=1
if restart_noctalia_with_launcher qs >/dev/null 2>&1; then
  echo "restart unexpectedly launched while an old instance remained" >&2
  exit 1
fi
if [[ -e "${SHAULA_TEST_QS_ARGS}" ]]; then
  echo "launcher ran despite stale instance" >&2
  exit 1
fi

: >"${SHAULA_TEST_QS_STATE}"
export SHAULA_TEST_QS_STICKY=0
export SHAULA_TEST_SYSTEMD_RESTART=success
export SHAULA_TEST_SYSTEMD_INSTANCE=replace
restart_noctalia >/dev/null
assert_equal service-new "$(cat "${SHAULA_TEST_QS_STATE}")" systemd-instance

printf '%s' service-old >"${SHAULA_TEST_QS_STATE}"
export SHAULA_TEST_SYSTEMD_INSTANCE=keep
export SHAULA_NOCTALIA_READY_ATTEMPTS=2
if restart_noctalia >/dev/null 2>&1; then
  echo "systemd restart unexpectedly accepted the pre-restart instance" >&2
  exit 1
fi
assert_equal service-old "$(cat "${SHAULA_TEST_QS_STATE}")" stale-systemd-instance

export SHAULA_TEST_SYSTEMD_INSTANCE=replace
export SHAULA_NOCTALIA_READY_ATTEMPTS=20
restart_noctalia >/dev/null
assert_equal service-new "$(cat "${SHAULA_TEST_QS_STATE}")" replaced-systemd-instance

: >"${SHAULA_TEST_QS_STATE}"
export SHAULA_TEST_SYSTEMD_INSTANCE=none
export SHAULA_NOCTALIA_READY_ATTEMPTS=2
if restart_noctalia >/dev/null 2>&1; then
  echo "systemd restart unexpectedly passed without a ready instance" >&2
  exit 1
fi

printf 'ok dev noctalia tests\n'
