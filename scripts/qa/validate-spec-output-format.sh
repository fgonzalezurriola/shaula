#!/bin/bash
set -euo pipefail

FILE="${1:-spec/algo.md}"

if [ ! -f "$FILE" ]; then
    echo "Error: File $FILE not found"
    exit 1
fi

REQUIRED_SECTIONS=(
    "Executive Technical Summary"
    "Decision Register"
    "MVP Capability Matrix"
    "AGENT-FIRST CLI"
    "Performance Budgets"
    "Uncertainty / Pending Verification List"
    "Risk and Dependency Matrix"
    "References"
    "Technical Summary"
)

ERR_COUNT=0

for SECTION in "${REQUIRED_SECTIONS[@]}"; do
    if ! grep -q "## $SECTION" "$FILE"; then
        echo "Error: Missing required section '## $SECTION' in $FILE"
        ERR_COUNT=$((ERR_COUNT + 1))
    fi
done

if [ "$ERR_COUNT" -gt 0 ]; then
    echo "Validation failed: $ERR_COUNT missing sections."
    exit 1
else
    echo "Format validation for $FILE passed."
    exit 0
fi
