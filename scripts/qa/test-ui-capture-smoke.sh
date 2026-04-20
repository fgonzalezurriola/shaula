#!/usr/bin/env bash
set -euo pipefail

echo "=== QA: UI Capture Smoke Test ==="

export SHAULA_BIN="./mock-shaula-smoke"

cat << 'MOCK' > "$SHAULA_BIN"
#!/usr/bin/env bash
if [[ "$1" == "capabilities" ]]; then
    echo '{"ok":true,"contract_version":"1.0.0","command":"capabilities list","result":{"foo":"bar"}}'
elif [[ "$1" == "history" ]]; then
    echo '{"ok":true,"contract_version":"1.0.0","command":"history list","result":[]}'
elif [[ "$1" == "capture" ]]; then
    echo '{"ok":true,"contract_version":"1.0.0","command":"capture '$2'","result":{"mode":"'$2'"}}'
fi
MOCK
chmod +x "$SHAULA_BIN"

UI_SCRIPT="src/ui/cli_wrapper.sh"

# Run launcher (capabilities test)
$UI_SCRIPT launcher | grep -q "\[UI:STATE:READY\]" || { echo "FAIL: Launcher failed smoke test"; exit 1; }

# Run history
$UI_SCRIPT history | grep -q "\[UI:STATE:READY\]" || { echo "FAIL: History failed smoke test"; exit 1; }

# Run overlay area
$UI_SCRIPT overlay area | grep -q "\[UI:STATE:READY\]" || { echo "FAIL: Overlay area failed smoke test"; exit 1; }

echo "PASS: UI capture smoke test completed successfully"
rm "$SHAULA_BIN"
