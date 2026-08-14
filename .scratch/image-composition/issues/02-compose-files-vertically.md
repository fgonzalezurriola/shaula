# Compose files vertically on a bounded canvas

Type: AFK
Status: resolved

## Parent

`.scratch/image-composition/spec.md`

## What to build

Let a user choose a bounded ordered set of image files, preview a vertical
layout, and apply it to an expanded Preview document. Imported images remain
editable, the operation is one undo entry, exports use the expanded bounds, and
cancel leaves the document unchanged.

## Acceptance criteria

- [x] Up to four valid image files can be ordered and composed vertically.
- [x] Apply expands the canvas, inserts editable images, selects them, and refits the view.
- [x] Undo/redo, crop, copy, save, and Done reproduce the correct composed pixels.
- [x] Invalid or oversized plans fail before mutation with deterministic ownership and cleanup.

## Blocked by

None - canvas contracts are approved in ADR-0005.

## Answer

Implemented in Preview's bounded composition sheet. File sources can be
ordered, previewed, and applied as editable Image annotations in one
`compose images` history mutation. The placement is preflighted against the
16,384 px/32 MP per-image and 64 MP output limits; cancellation and rejected
plans do not mutate the document. Shared image references keep undo/redo and
export deterministic.
