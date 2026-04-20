#!/usr/bin/env bash
set -euo pipefail

SPEC_DIR="${1:-spec}"
REQUIRED_FILES=(
    "algo.md"
    "requirements.md"
    "architecture.md"
    "wayland-niri-integration.md"
    "performance.md"
    "testing.md"
    "phases.md"
)

ERR_COUNT=0

for FILE in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$SPEC_DIR/$FILE" ]; then
        echo "Error: Required spec file missing: $SPEC_DIR/$FILE"
        ERR_COUNT=$((ERR_COUNT + 1))
    fi
done

if [ "$ERR_COUNT" -gt 0 ]; then
    echo "ERR_SPEC_INCONSISTENT_DECISIONS"
    exit 1
fi

bash scripts/qa/validate-spec-cross-links.sh "$SPEC_DIR" || ERR_COUNT=$((ERR_COUNT + 1))
bash scripts/qa/validate-spec-output-format.sh "$SPEC_DIR/algo.md" || ERR_COUNT=$((ERR_COUNT + 1))

if [ "$ERR_COUNT" -gt 0 ]; then
    echo "ERR_SPEC_INCONSISTENT_DECISIONS"
    exit 1
else
    echo "All spec files are consistent and validated."
    exit 0
fi
