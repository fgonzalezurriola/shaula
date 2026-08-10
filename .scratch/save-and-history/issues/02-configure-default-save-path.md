# Configure the default save path

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Let users choose and persist a default screenshot directory, validate it at the
runtime boundary, and use it consistently across save suggestions, screenshots
folder actions, notifications, and recovery from unavailable locations.

## Acceptance criteria

- [ ] Settings can choose, validate, persist, and display the default directory.
- [ ] Save flows and open-folder actions resolve the same configured location.
- [ ] Missing or unwritable locations produce actionable behavior without data loss.
- [ ] Explicit Save As destinations continue to override the default safely.

## Blocked by

None - can start immediately.
