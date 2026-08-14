# Add direct bounded canvas controls

Type: HITL
Status: resolved

## Parent

`.scratch/image-composition/spec.md`

## What to build

Design and deliver direct controls for expanding the canvas in the approved
directions, choosing its background, and repositioning existing content. The
workflow must remain bounded, explicit, undoable, and consistent with crop and
export semantics.

## Acceptance criteria

- [x] The control design is reviewed in the real Preview UI before final implementation.
- [x] Expansion translates content according to the approved coordinate contract.
- [x] Each confirmed resize is one undo entry; cancellation has no output effect.
- [x] Copy, Save, Done, crop, Spotlight, selection, and Fit to screen use the resulting canvas.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`

## Answer

Implemented as `Expand canvas…` in the More menu. The approved first-slice
coordinate contract keeps existing content at the same origin and adds bounded
space only to the right and/or bottom, with an opaque background palette.
Validation occurs before the edit session starts; confirmation creates one
`expand canvas` undo step and refits the document. Cancel closes the dialog with
no history or pixel mutation.
