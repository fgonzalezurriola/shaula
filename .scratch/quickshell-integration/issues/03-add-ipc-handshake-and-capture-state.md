# Add QuickShell IPC handshake and capture state

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/quickshell-integration/spec.md`

## What to build

Add the approved bidirectional IPC path so a QuickShell integration can hide
its panel before capture and display capture lifecycle state. Preserve the
existing safe fallback and deterministic capture-session outcomes when IPC is
absent, stale, or malformed.

## Acceptance criteria

- [ ] Panel hide/show coordination completes through IPC when available.
- [ ] Missing or failed IPC follows the approved fallback without capturing the panel.
- [ ] Widgets expose accurate in-progress, completion, and failure state.
- [ ] Capture concurrency, timeout, cleanup, JSON, and error-taxonomy contracts remain deterministic.

## Blocked by

- `.scratch/quickshell-integration/issues/02-extract-service-and-migrate-noctalia.md`
