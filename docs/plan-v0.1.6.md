# Original Shaula v0.1.6 Image Composition Plan

> Status: **Superseded as a release-number plan**
>
> The image-composition design remains future product work, but v0.1.6 now ships
> the Meson/C, universal Wayland, clipboard lifecycle, and dual-architecture
> release work documented in `docs/release-v0.1.6.md`. This file is retained as
> historical design input and must not be used as the v0.1.6 release checklist.
>
> Original baseline: v0.1.5

## 1. Original Release Direction

The original proposal made v0.1.6 the **image composition and expandable canvas
release**.

The release builds on the stable v0.1.5 capture and Preview workflow. Its main
product goal is to let users combine screenshots or other images in one editable
Preview document without forcing them to keep irrelevant content between
captures.

The release may take several weeks or longer. There is no deadline pressure.
Correct document ownership, predictable undo/redo, bounded memory use, and a
human-friendly workflow take priority over shipping quickly.

Detailed product behavior lives in `docs/plan-image-composition.md`. The
underlying technical direction lives in
`docs/preview-expandable-canvas-design.md`.

## 2. Scope Model

The release uses three scope lanes.

### 2.1 Core committed work

These capabilities define v0.1.6 and must be coherent before release:

- explicit bounded canvas dimensions independent from the original screenshot;
- safe canvas expansion with undo and redo;
- vertical and horizontal image composition;
- images imported from the current clipboard and explicit files;
- editable imported Image annotations rather than immediate flattening;
- deterministic ordering, gap, alignment, and layout-aware sizing;
- full expanded-canvas support in Copy, Save, Save As, Done, crop, Spotlight,
  selection, movement, resize, and export;
- one history entry for one composition apply;
- automatic Fit to screen after document-size changes;
- explicit dimension, pixel, and memory guards;
- no global clipboard-history recorder.

### 2.2 Planned additions

These belong to the v0.1.6 feature family and should be included when their
contracts are reliable:

- recent Shaula captures as a composition source;
- direct canvas controls for adding space on each side;
- canvas background controls;
- `Capture and add…` from an existing Preview;
- grid layout and equal-cell comparison layouts;
- simple alignment or distribution guides for free placement;
- clearer document dimension and composition feedback.

These additions must not appear as disabled placeholders. A capability enters
the public UI only when its document, history, cancellation, error, and export
paths are complete.

### 2.3 Emergent work

Work discovered during implementation may enter v0.1.6 when it does one of the
following:

- fixes a bug exposed by composition or canvas expansion;
- removes a confusing Preview or capture interaction;
- reduces memory, rendering, history, or ownership risk;
- improves testability or module boundaries needed by the release;
- improves installation, settings, documentation, or integration consistency;
- addresses a concrete issue found through normal daily use.

Emergent work is not an invitation to add unrelated large features. Each item
should either support the v0.1.6 direction or be a small, high-value improvement
that is safer to finish now than defer.

## 3. Product Workflow

### 3.1 Add images

The primary entry point is a More-menu action:

- Label: `Add images…`
- Tooltip: `Add images and expand canvas`

It opens a compact composition sheet. The sheet should support:

- image thumbnails;
- selection and reorder;
- vertical or horizontal layout;
- gap;
- alignment;
- original-size or layout-aware sizing;
- a small result preview;
- explicit Apply and Cancel actions.

Initial operations should remain bounded to a small number of images, with four
as the starting product limit until memory measurements justify another value.

### 3.2 Direct canvas expansion

Users should be able to add space around the existing document without first
importing an image. The first usable design should support:

- adding space above, below, left, or right;
- exact or preset amounts;
- visible canvas background selection;
- one undoable operation;
- deterministic translation of existing content when adding space above or
  left.

This remains a bounded raster canvas, not an infinite workspace.

### 3.3 Capture and add

A later v0.1.6 milestone should allow another capture to return to the existing
Preview document:

1. Start with an open Preview.
2. Invoke `Capture and add…`.
3. Complete Quick or Area capture.
4. Return the result to the originating Preview.
5. Offer vertical, horizontal, or free placement.
6. Apply as one undoable document edit.

This flow must preserve the capture-session lock and short-lived helper
architecture. It must not introduce a resident daemon.

## 4. Technical Milestones

### Milestone 1: Explicit document canvas

- Add canvas dimensions independent from the base pixbuf.
- Introduce explicit base-image placement.
- Rename ambiguous dimension helpers so canvas, asset, and viewport bounds are
  distinguishable.
- Preserve all current visible behavior while the model changes underneath.

### Milestone 2: Shared image ownership and history

- Introduce immutable shared image assets.
- Prevent history snapshots from duplicating full pixel payloads.
- Include canvas size, background, base placement, annotations, Spotlight, and
  asset references in snapshots.
- Verify lifetime across undo, redo, duplicate, crop, clipboard, and Preview
  destruction.

### Milestone 3: Canvas-aware rendering and editing

- Render and export using canvas bounds.
- Audit movement, resize, selection, marquee, paste placement, crop, Spotlight,
  toolbar metadata, and initial fit.
- Add atomic canvas resize and content translation.
- Define and test canvas-size and total-pixel limits.

### Milestone 4: Structured composition engine

- Build a pure deterministic placement planner.
- Support vertical and horizontal layouts.
- Apply resize plus multiple image insertions atomically.
- Roll back the full operation on failure.
- Select only the inserted set and refit the view.

### Milestone 5: Composition UI and sources

- Add the composition sheet.
- Support current clipboard and files.
- Add recent Shaula captures after retention and cleanup contracts are resolved.
- Keep source loading asynchronous and cancellation-safe.

### Milestone 6: Direct canvas controls

- Add space in four directions.
- Add background controls.
- Add useful presets without hiding exact values.
- Preserve stable toolbar width by placing controls in a sheet or canvas HUD.

### Milestone 7: Capture and add

- Define the return channel to the originating Preview.
- Preserve capture lock and cancellation semantics.
- Avoid duplicate previews or durable files unless the user explicitly saves.
- Test window closure and provider disappearance during the flow.

### Milestone 8: Release polish

- Resolve issues discovered through daily use.
- Update screenshots, README, roadmap, Preview docs, and release notes.
- Validate installer, AUR metadata, Noctalia payload, and archive contents.
- Run manual Wayland/Niri journeys on the final candidate.

## 5. Decisions to Resolve Early

The implementation should settle these before exposing public UI:

- opaque versus transparent canvas background;
- whether the original screenshot remains a non-selectable base layer;
- exact maximum canvas dimensions and pixel count;
- session-only versus persisted recent Shaula captures;
- recent-capture byte budget and cleanup ownership;
- crop behavior for partially intersecting Image annotations;
- imported-image ordering relative to Spotlight;
- the first supported `Capture and add…` modes;
- whether four imported images remains the correct initial limit.

Durable architectural decisions should become ADRs when implementation begins.

## 6. Quality Rules

- No public placeholder actions.
- No silent fallback from a failed composition to a flattened or partial result.
- No rereading clipboard or files during redo.
- No unrelated global clipboard monitoring.
- No mutation before all source images and placement plans validate.
- No history entry for canceled or failed operations.
- No toolbar natural-width growth from contextual composition controls.
- No claims of compositor support without direct evidence.

## 7. Release Bar

v0.1.6 is ready when:

- the core committed work is complete;
- navbar/footer and side-by-side comparison workflows are straightforward;
- undo and redo restore canvas bounds and pixels deterministically;
- Copy, Save, Save As, Done, crop, and Spotlight work across the expanded canvas;
- memory use remains bounded under the supported image and history limits;
- cancel and failure paths leave the document unchanged;
- existing v0.1.5 capture and Preview behavior has no known high-impact
  regression;
- the automated baseline passes;
- the final candidate receives manual Wayland/Niri validation.

Required automated checks:

```bash
./dev check
git diff --check
```

After UI changes:

```bash
./dev dev-install --yes
```

Manual interactive validation should include:

```bash
./dev capture
./dev all
```

## 8. Explicit Non-Goals

v0.1.6 does not target:

- infinite canvas;
- scrolling capture;
- screen recording;
- OCR;
- AI removal or automatic background removal;
- smart semantic selection;
- global clipboard history;
- multi-page documents;
- cloud upload, Share, or collaboration;
- persistent Pin windows;
- perspective transforms or freeform image distortion;
- a general-purpose layer panel or design application.

## 9. Planning Cadence

This plan is intentionally not tied to a calendar date. Work can proceed during
the week, across the following month, or at another comfortable pace.

The plan should be updated when implementation reveals a better interaction,
changes a durable contract, or adds a meaningful emergent item. Release scope is
judged by product coherence and evidence, not by elapsed time.
