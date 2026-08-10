# Add direct bounded canvas controls

Type: HITL
Status: ready-for-human

## Parent

`.scratch/image-composition/spec.md`

## What to build

Design and deliver direct controls for expanding the canvas in the approved
directions, choosing its background, and repositioning existing content. The
workflow must remain bounded, explicit, undoable, and consistent with crop and
export semantics.

## Acceptance criteria

- [ ] The control design is reviewed in the real Preview UI before final implementation.
- [ ] Expansion translates content according to the approved coordinate contract.
- [ ] Each confirmed resize is one undo entry; cancellation has no output effect.
- [ ] Copy, Save, Done, crop, Spotlight, selection, and Fit to screen use the resulting canvas.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`
