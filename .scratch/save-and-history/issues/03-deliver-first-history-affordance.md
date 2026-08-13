# Deliver the first additional Preview history affordance

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Show the next concrete operation in the Undo and Redo controls, such as
`Undo crop` or `Redo arrow`, so the user can predict their effect without
exposing internal snapshots. Keep the ordinary Undo or Redo label when no
operation is available, and preserve the existing shortcuts and history bound.

## Acceptance criteria

- [x] The target user problem and rejected alternatives are documented before implementation.
- [ ] The proposed interaction is reviewed in the real Preview UI.
- [ ] The affordance stays synchronized with undo/redo availability and document mutations.
- [ ] Existing keyboard shortcuts and history bounds remain intact.

## Blocked by

None - can start immediately.

## Decision

Approved on 2026-08-12. Prefer action-aware Undo and Redo labels or tooltips as
the smallest useful affordance. A navigable history list and a snapshot
timeline are intentionally deferred because they add interaction and state
without first proving that users need arbitrary history navigation.
