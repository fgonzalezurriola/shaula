# Resolve bounded canvas document contracts

Type: HITL
Status: resolved

## Parent

`.scratch/image-composition/spec.md`

## What to build

Resolve the open durable choices required before coding: initial expansion
directions, canvas limits, base-image selection semantics, background behavior,
crop behavior, recent-capture lifetime, and atomic rollback expectations.
Record decisions that make the first composition slice implementable.

## Acceptance criteria

- [x] Every blocking question in the expandable-canvas design has a decision.
- [x] Canvas, base image, asset ownership, history, crop, and error contracts agree.
- [x] The approved first-slice limits and non-goals are recorded in durable docs.

## Blocked by

None - can start immediately.

## Decision

Approved on 2026-08-12 and recorded in ADR-0005. The first slice expands only
right and down, uses an opaque neutral canvas, keeps the base image
non-selectable, preserves editable image assets across crop, and applies each
composition atomically. Recent captures are Shaula-owned, persisted, bounded,
and pruned automatically.
