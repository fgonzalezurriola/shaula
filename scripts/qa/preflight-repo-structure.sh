#!/bin/bash
set -e

TARGET_DIR="${1:-.}"

REQUIRED_DIRS=(
    "src"
    "spec"
    "scripts/qa"
    "tests"
    ".qa/evidence"
)

REQUIRED_FILES=(
    "README.md"
    "CONTRIBUTING.md"
)

for dir in "${REQUIRED_DIRS[@]}"; do
    if [[ ! -d "$TARGET_DIR/$dir" ]]; then
        echo "ERROR: Missing directory $dir"
        echo "ERR_REPO_STRUCTURE_INVALID"
        exit 1
    fi
done

for file in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "$TARGET_DIR/$file" ]]; then
        echo "ERROR: Missing file $file"
        echo "ERR_REPO_STRUCTURE_INVALID"
        exit 1
    fi
done

echo "Repository structure is valid."
exit 0
