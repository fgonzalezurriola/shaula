# Configure the default save path

Type: AFK
Status: resolved

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Let users choose and persist a default screenshot directory, validate it at the
runtime boundary, and use it consistently across save suggestions, screenshots
folder actions, notifications, and recovery from unavailable locations.

## Acceptance criteria

- [x] Settings can choose, validate, persist, and display the default directory.
- [x] Save flows and open-folder actions resolve the same configured location.
- [x] Missing or unwritable locations produce actionable behavior without data loss.
- [x] Explicit Save As destinations continue to override the default safely.

## Resolution

Resolved on 2026-08-12. Capture, Preview, Save As, and the screenshots-folder
command share one validated runtime resolver.

## Blocked by

None - can start immediately.
