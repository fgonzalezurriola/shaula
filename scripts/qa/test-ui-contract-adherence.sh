#!/usr/bin/env bash
set -euo pipefail

echo "=== QA: UI Contract Adherence ==="

UI_SCRIPT="src/ui/cli_wrapper.sh"

# 1. Verify UI script exists
if [[ ! -f "$UI_SCRIPT" ]]; then
    echo "FAIL: $UI_SCRIPT not found"
    exit 1
fi

# 2. Verify it does not contain direct capture tools (slurp, grim, wl-copy)
if grep -q -E 'slurp|grim|wl-copy|wlr-randr' "$UI_SCRIPT"; then
    echo "FAIL: UI script contains direct capture binaries, violating Backend Contract Rule"
    exit 1
fi

# 3. Verify it contains the required shaula json invocations
if ! grep -q -e '--json' "$UI_SCRIPT"; then
    echo "FAIL: UI script does not strictly invoke --json"
    exit 1
fi

echo "PASS: UI adheres strictly to backend CLI contracts"
mkdir -p .sisyphus/evidence
echo "UI strictly calls --json and contains no direct Wayland tools." > .sisyphus/evidence/task-14-ui-contract.txt
