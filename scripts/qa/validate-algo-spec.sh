#!/bin/bash
set -e

SPEC_FILE=$1

if [ -z "$SPEC_FILE" ]; then
    echo "Usage: $0 <spec_file>"
    exit 1
fi

REQUIRED_SECTIONS=(
    "Decision Register"
    "MVP Capability Matrix"
    "AGENT-FIRST CLI"
)

MISSING=0
for section in "${REQUIRED_SECTIONS[@]}"; do
    if ! grep -q "## $section" "$SPEC_FILE"; then
        echo "ERR_ALGO_SPEC_MISSING_SECTION: '$section'"
        MISSING=1
    fi
done

if [ "$MISSING" -eq 1 ]; then
    exit 1
fi

echo "algo.md is valid: all required sections found."
exit 0
