#!/usr/bin/env bash

shaula_qa_manual_suite_warning() {
  local suite="${1:-unknown}"
  cat >&2 <<EOF
warning: ${suite} is a manual/legacy QA wrapper.
warning: The maintained non-intrusive gate is ./dev qa plus ./dev check and git diff --check.
warning: This wrapper may include stale task evidence, real Niri/Wayland, Noctalia, benchmark, or intrusive UI assumptions.
EOF
}
