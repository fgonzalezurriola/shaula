# Configure screenshot filename templates

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Let users configure a validated screenshot filename template and use it
consistently for generated save suggestions without changing explicit Save As
choices or producing unsafe paths.

## Acceptance criteria

- [ ] Settings explains and persists the supported template fields.
- [ ] Generated names are deterministic, filesystem-safe, and collision-aware.
- [ ] Invalid templates are rejected without replacing the last valid preference.
- [ ] Capture, Save, Save As, and notifications agree on the resulting filename.

## Blocked by

None - can start immediately.
