#!/usr/bin/env bash
set -euo pipefail

echo "=== QA: UI Error State Mapping ==="

export SHAULA_BIN="./mock-shaula"

# Create a mock shaula that returns an ERR_
cat << 'MOCK' > "$SHAULA_BIN"
#!/usr/bin/env bash
if [[ "$1" == "capture" && "$2" == "area" ]]; then
    echo '{"ok":false,"contract_version":"1.0.0","command":"capture area","timestamp":"now","error":{"code":"ERR_CAPTURE_BACKEND_UNAVAILABLE","message":"Mocked backend error","retryable":false}}'
fi
MOCK
chmod +x "$SHAULA_BIN"

UI_SCRIPT="src/ui/cli_wrapper.sh"

# Test overlay area
out=$($UI_SCRIPT overlay area) || true
echo "$out"

# Check mapping
if ! echo "$out" | grep -q "\[UI:STATE:ERROR\] Backend mapped error: ERR_CAPTURE_BACKEND_UNAVAILABLE"; then
    echo "FAIL: UI did not map ERR_CAPTURE_BACKEND_UNAVAILABLE correctly"
    rm "$SHAULA_BIN"
    exit 1
fi

echo "PASS: UI maps backend ERR_* semantics deterministicly to state"
rm "$SHAULA_BIN"

mkdir -p .qa/evidence
echo "UI correctly maps backend errors to [UI:STATE:ERROR] semantics." > .qa/evidence/task-14-ui-contract-error.txt
