#!/bin/bash
set -euo pipefail

SPEC_FILE="${1:-spec/requirements.md}"
PHASES_FILE="${2:-}"
ERR_TOKEN="ERR_FUTURE_FEATURE_GATE_MISSING"

extract_future_matrix_rows() {
    local file="$1"

    awk '
    BEGIN {
        in_section = 0
        saw_header = 0
    }
    /^##[[:space:]]+Future Feature Matrix[[:space:]]*$/ {
        in_section = 1
        next
    }
    /^##[[:space:]]+/ {
        if (in_section) {
            in_section = 0
        }
    }
    {
        if (!in_section) next
        if ($0 !~ /^\|/) next

        if ($0 ~ /^\|[[:space:]]*Feature[[:space:]]*\|/) {
            saw_header = 1
            next
        }
        if (!saw_header) next
        if ($0 ~ /^\|[[:space:]]*:---/) next

        print
    }
    ' "$file"
}

echo "Validating Future Feature Matrix in $SPEC_FILE..."

if [[ ! -f "$SPEC_FILE" ]]; then
    echo "FAILED: Missing requirements spec file: $SPEC_FILE"
    echo "$ERR_TOKEN"
    exit 1
fi

ROWS="$(extract_future_matrix_rows "$SPEC_FILE")"

if [[ -z "$ROWS" ]]; then
    echo "FAILED: No rows found under '## Future Feature Matrix' in $SPEC_FILE"
    echo "$ERR_TOKEN"
    exit 1
fi

FAIL=0
while IFS= read -r line; do
    [[ -z "$line" ]] && continue

    FEATURE="$(printf '%s\n' "$line" | awk -F'|' '{print $2}' | xargs)"
    PHASING="$(printf '%s\n' "$line" | awk -F'|' '{print $3}' | xargs)"
    FEASIBILITY="$(printf '%s\n' "$line" | awk -F'|' '{print $4}' | xargs)"
    DEPS="$(printf '%s\n' "$line" | awk -F'|' '{print $5}' | xargs)"
    RISK="$(printf '%s\n' "$line" | awk -F'|' '{print $6}' | xargs)"
    METRIC="$(printf '%s\n' "$line" | awk -F'|' '{print $7}' | xargs)"

    if [[ -z "$FEATURE" || -z "$PHASING" || -z "$FEASIBILITY" || -z "$DEPS" || -z "$RISK" || -z "$METRIC" ]]; then
        echo "FAILED: Feature '$FEATURE' has missing required matrix fields."
        echo "$ERR_TOKEN"
        FAIL=1
        break
    fi
done <<< "$ROWS"

if [[ -n "$PHASES_FILE" ]]; then
    if [[ ! -f "$PHASES_FILE" ]]; then
        echo "FAILED: Missing phases spec file: $PHASES_FILE"
        echo "$ERR_TOKEN"
        exit 1
    fi

    if ! grep -q "Go/No-Go" "$PHASES_FILE"; then
        echo "FAILED: Missing Go/No-Go marker in $PHASES_FILE"
        echo "$ERR_TOKEN"
        exit 1
    fi
fi

if [[ $FAIL -eq 1 ]]; then
    exit 1
fi

echo "Validation SUCCESS: Future Feature Matrix rows are complete and Go/No-Go gating is present."
exit 0
