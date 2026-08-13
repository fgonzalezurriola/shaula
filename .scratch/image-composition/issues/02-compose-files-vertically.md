# Compose files vertically on a bounded canvas

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/image-composition/spec.md`

## What to build

Let a user choose a bounded ordered set of image files, preview a vertical
layout, and apply it to an expanded Preview document. Imported images remain
editable, the operation is one undo entry, exports use the expanded bounds, and
cancel leaves the document unchanged.

## Acceptance criteria

- [ ] Up to four valid image files can be ordered and composed vertically.
- [ ] Apply expands the canvas, inserts editable images, selects them, and refits the view.
- [ ] Undo/redo, crop, copy, save, and Done reproduce the correct composed pixels.
- [ ] Invalid or oversized plans fail before mutation with deterministic ownership and cleanup.

## Blocked by

None - canvas contracts are approved in ADR-0005.
