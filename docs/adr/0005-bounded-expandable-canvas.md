# ADR 0005: Bounded expandable canvas contracts

## Status

Accepted.

## Context

Image composition requires document bounds that can exceed the original
screenshot without introducing an infinite canvas, unbounded memory use, or
flattened imported images. The first vertical file-composition slice needs
stable canvas, asset, crop, history, rendering, and recent-capture contracts.

## Decision

The first slice uses an opaque neutral canvas and expands only toward the right
and bottom. The original screenshot becomes a placed, non-selectable base image
that is never resampled by canvas expansion. Imported images retain original
pixel size by default and remain editable Image annotations.

Immutable image pixels are owned by a `GObject`-based image asset shared by the
document, annotations, the local clipboard, and history snapshots. Snapshot
copies retain assets rather than duplicating their pixels.

One composition operation accepts at most four imported images. Each image is
limited to 16,384 pixels per side and 32 megapixels, matching the current system
paste boundary. The resulting canvas and aggregate newly decoded assets are
each limited to 64 megapixels. Allocation failures remain explicit and cannot
partially mutate the document.

Composition is preflighted completely, then applied as one history mutation.
If an error occurs after mutation starts, the pending snapshot restores canvas
bounds, base placement, annotations, Spotlight regions, selection-relevant
document state, IDs, and modified state.

Crop operates in canvas coordinates. Partially intersecting Image annotations
remain editable and retain their immutable source asset while output outside
the cropped canvas is clipped. Imported images render above Spotlight, matching
the existing annotation render order.

Recent captures persist across launches in a Shaula-owned cache. The cache
contains only Shaula-created captures, retains at most 12 items and 256 MiB of
encoded data, and prunes oldest entries first after registration and at startup.
Missing entries are evicted and reported as unavailable without mutating the
document. This does not create clipboard history.

## Consequences

The first composition UI cannot add space above or left, export transparency,
or select the base screenshot. Those capabilities require later explicit
document and UI decisions. Memory remains bounded even with shared history, and
copy, save, crop, undo, and redo can operate without rereading source files or
the system clipboard.
