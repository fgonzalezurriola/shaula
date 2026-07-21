#!/usr/bin/env bash
set -euo pipefail

PROJECT_NAME="${CF_PAGES_PROJECT:-shaula-screenshotter}"
PRODUCTION_BRANCH="${CF_PAGES_BRANCH:-master}"
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="$ROOT_DIR/web"
WRANGLER=(pnpm dlx wrangler@latest)

cd "$WEB_DIR"
pnpm install --frozen-lockfile
pnpm build

# The upload is intentionally manual: this project has no runtime or CI deploy.
"${WRANGLER[@]}" pages deploy dist \
  --project-name "$PROJECT_NAME" \
  --branch "$PRODUCTION_BRANCH" \
  --commit-dirty
