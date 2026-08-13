# Configure screenshot filename templates

Type: AFK
Status: resolved

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Let users configure a validated screenshot filename template and use it
consistently for generated save suggestions without changing explicit Save As
choices or producing unsafe paths.

## Acceptance criteria

- [x] Settings explains and persists the supported template fields.
- [x] Generated names are deterministic, filesystem-safe, and collision-aware.
- [x] Invalid templates are rejected without replacing the last valid preference.
- [x] Capture, Save, Save As, and notifications agree on the resulting filename.

## Resolution

Resolved on 2026-08-12. `{date}`, `{time}`, and `{datetime}` are supported;
`.png` and collision suffixes are owned by the shared save-destination runtime.

## Blocked by

None - can start immediately.
