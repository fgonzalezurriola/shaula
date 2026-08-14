# Compose clipboard images horizontally

Type: AFK
Status: resolved

## Parent

`.scratch/image-composition/spec.md`

## What to build

Extend the composition flow with the current clipboard as an explicit source
and horizontal placement, preserving clipboard cancellation and ownership while
sharing the same bounded preview, history, rendering, and export behavior.

## Acceptance criteria

- [x] The current clipboard image can be combined with selected files without observing clipboard history.
- [x] Horizontal layouts support the approved gap, alignment, and sizing controls.
- [x] Clipboard replacement, cancellation, invalid payloads, and window closure cannot partially mutate the document.
- [x] The complete operation remains one undoable document edit.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`

## Answer

Implemented with one explicit GTK clipboard texture read per Add clipboard
request. Cancellation and closing are guarded by a ref-counted dialog context;
decoded pixels are owned by the composition item before apply. The clipboard
`changed` signal cancels an in-flight read and rejects its decoded result, so a
replacement cannot become a stale composition source. Horizontal placement
shares the same bounded plan and creates one `compose images` undo entry.
