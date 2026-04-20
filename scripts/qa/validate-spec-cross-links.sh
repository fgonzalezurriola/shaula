#!/bin/bash
set -euo pipefail

TARGET_DIR="${1:-spec}"
ERR_COUNT=0

FILES=$(find "$TARGET_DIR" -name "*.md")

for FILE in $FILES; do
    LINKS=$(grep -oP '\[.*?\]\(\K.*?(?=\))' "$FILE" || true)
    
    for LINK in $LINKS; do
        if [[ "$LINK" =~ ^http ]]; then
            continue
        fi
        
        BASE_DIR=$(dirname "$FILE")
        LINK_PATH="$BASE_DIR/$LINK"
        CLEAN_LINK_PATH="${LINK_PATH%%#*}"
        
        if [ ! -f "$CLEAN_LINK_PATH" ]; then
            echo "Error: Broken link in $FILE -> $LINK (path: $CLEAN_LINK_PATH)"
            ERR_COUNT=$((ERR_COUNT + 1))
        fi
    done
done

if [ "$ERR_COUNT" -gt 0 ]; then
    echo "Found $ERR_COUNT broken links."
    exit 1
else
    echo "All cross-links validated successfully."
    exit 0
fi
