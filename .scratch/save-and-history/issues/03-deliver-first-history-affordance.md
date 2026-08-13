# Deliver the first additional Preview history affordance

Type: AFK
Status: resolved

## Parent

`.scratch/save-and-history/spec.md`

## What to build

Show the next concrete operation in the Undo and Redo controls, such as
`Undo crop` or `Redo arrow`, so the user can predict their effect without
exposing internal snapshots. Keep the ordinary Undo or Redo label when no
operation is available, and preserve the existing shortcuts and history bound.

## Acceptance criteria

- [x] The target user problem and rejected alternatives are documented before implementation.
- [x] The proposed interaction is reviewed against the real Preview toolbar implementation.
- [x] The affordance stays synchronized with undo/redo availability and document mutations.
- [x] Existing keyboard shortcuts and history bounds remain intact.

## Blocked by

None - can start immediately.

## Decision

Approved on 2026-08-12. Prefer action-aware Undo and Redo labels or tooltips as
the smallest useful affordance. A navigable history list and a snapshot
timeline are intentionally deferred because they add interaction and state
without first proving that users need arbitrary history navigation.

## Resolution

Resolved on 2026-08-12. History snapshots carry a user-facing operation name,
which transfers between Undo and Redo and updates both tooltip and accessible
label without changing the 24-entry bound.
