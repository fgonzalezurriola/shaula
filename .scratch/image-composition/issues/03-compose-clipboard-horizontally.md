# Compose clipboard images horizontally

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/image-composition/spec.md`

## What to build

Extend the composition flow with the current clipboard as an explicit source
and horizontal placement, preserving clipboard cancellation and ownership while
sharing the same bounded preview, history, rendering, and export behavior.

## Acceptance criteria

- [ ] The current clipboard image can be combined with selected files without observing clipboard history.
- [ ] Horizontal layouts support the approved gap, alignment, and sizing controls.
- [ ] Clipboard replacement, cancellation, invalid payloads, and window closure cannot partially mutate the document.
- [ ] The complete operation remains one undoable document edit.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`
