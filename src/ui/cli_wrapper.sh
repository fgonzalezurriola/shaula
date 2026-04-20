#!/usr/bin/env bash

# Shaula UI Backend Contract Adherence Wrapper

SHAULA_BIN="${SHAULA_BIN:-./zig-out/bin/shaula}"

handle_state() {
    local json="$1"
    # Use grep to simulate json parsing
    local ok=$(echo "$json" | grep -o '"ok":true' || echo "false")
    
    if [[ "$ok" == '"ok":true' ]]; then
        echo "[UI:STATE:LOADING] Command starting..."
        echo "[UI:STATE:READY] Success!"
        echo "$json"
        return 0
    else
        local err_code=$(echo "$json" | grep -o '"code":"[^"]*"' | cut -d':' -f2 | tr -d '"')
        echo "[UI:STATE:ERROR] Backend mapped error: $err_code"
        echo "$json"
        return 1
    fi
}

cmd_launcher() {
    echo "[UI:LAUNCHER] Invoking UI Launcher..."
    # UI must ONLY call validated daemon contracts via CLI
    local out=$($SHAULA_BIN capabilities list --json 2>/dev/null)
    handle_state "$out"
}

cmd_history() {
    echo "[UI:HISTORY] Fetching quick list..."
    local out=$($SHAULA_BIN history list --json 2>/dev/null)
    handle_state "$out"
}

cmd_overlay() {
    local mode="${1:-area}"
    echo "[UI:OVERLAY] Triggering capture $mode..."
    local out=$($SHAULA_BIN capture "$mode" --json 2>/dev/null)
    handle_state "$out"
}

case "$1" in
    launcher) cmd_launcher ;;
    history) cmd_history ;;
    overlay) cmd_overlay "$2" ;;
    *) echo "Usage: $0 {launcher|history|overlay <mode>}"; exit 1 ;;
esac
