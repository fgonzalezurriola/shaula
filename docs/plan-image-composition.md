# Image Composition Plan

> Status: **Approved product direction** — Planned for phased implementation
>
> Release scope: **v0.1.6**
>
> Owner: Shaula project

## 1. Purpose

Add a focused workflow for combining screenshots and other images without
turning Preview into a general-purpose design application.

The primary use case is removing irrelevant space between related captures. For
example, a user may capture a page navbar and footer separately, then place the
footer directly below the navbar. The same workflow should also support images
from unrelated sources when the user wants a compact comparison, sequence, or
reference sheet.

This plan separates two product capabilities:

- **Image composition**: arrange a small set of images with a structured layout.
- **Expandable canvas**: increase the document bounds so images can exist beyond
  the original screenshot.

Image composition is the first product target. Expandable canvas is the document
capability that enables it and may later receive direct controls.

## 2. Scope Boundary

Image composition and bounded canvas expansion are the central product direction
for v0.1.6. Implementation should proceed in phases and preserve the stable
v0.1.5 capture and Preview behavior throughout the work.

The release has no fixed calendar deadline. Correct document ownership,
predictable history, bounded memory use, and a usable composition flow take
priority over speed.

This plan does not authorize placeholders in the public UI. Entry points should
appear only when the underlying document, history, export, cancellation, and
memory contracts are complete.

## 3. Core User Scenarios

### 3.1 Stitch related captures

A user captures two regions from the same page and places them vertically or
horizontally without the content between them.

Examples:

- navbar above footer;
- before/after states side by side;
- two sections of a long settings page;
- two monitor regions in one compact image.

### 3.2 Compose unrelated images

A user combines images from the current clipboard, recent Shaula captures, or
files into one output.

Examples:

- reference image beside a screenshot;
- visual comparison of two applications;
- four screenshots in a small grid;
- image plus explanatory annotations.

### 3.3 Continue editing after composition

The composed images remain selectable Image annotations. The user can move,
resize, annotate, undo, copy, and save the result instead of receiving an
immediately flattened bitmap.

## 4. Recommended Entry Point

Add one More-menu action when the feature is ready:

- Label: `Add images…`
- Tooltip: `Add images and expand canvas`

The tooltip explains the action but does not contain interactive choices. The
action opens a compact composition sheet or popover.

A permanent primary-toolbar button is not recommended. Composition is useful
but secondary, and the Preview toolbar has a stable-width contract.

## 5. Composition Sheet

### 5.1 Sources

Initial sources:

- **Current clipboard**: read the current image payload only.
- **Recent Shaula captures**: show images Shaula already created or retained.
- **Choose files…**: import explicit image files.

A later v0.1.6 milestone should add **Capture and add…**, which starts another
capture and returns the result to the existing Preview document when the return
channel and cancellation contracts are reliable.

### 5.2 Selection

The sheet shows image thumbnails with dimensions and source metadata. Users can
select and reorder a small bounded set.

Initial limits:

- maximum four images per composition operation;
- one thumbnail order shared by preview and final insertion;
- clear feedback when an image is invalid, oversized, or unavailable.

The four-image limit is a product and memory guard, not a permanent file-format
constraint.

### 5.3 Layouts

First implementation:

- **Vertical**
- **Horizontal**

Follow-up layouts:

- **Grid**
- **Free placement**

Layout options:

- gap: 0, 8, 16, or 32 pixels;
- alignment: start, center, or end;
- sizing: original size, match width for vertical layouts, or match height for
  horizontal layouts;
- background: a neutral default with an explicit color choice in a later phase.

The sheet should show a small live preview before applying the operation.

## 6. Apply Behavior

Confirming the sheet should:

1. Expand the document canvas to contain the result.
2. Insert each imported image as an editable Image annotation.
3. Preserve image aspect ratios.
4. Select the inserted image set.
5. Record the whole composition as one undo entry.
6. Recalculate Fit to screen so the complete result remains visible.
7. Keep Preview open for annotation, adjustment, copy, or save.

Canceling the sheet must not mutate the document or history.

## 7. Clipboard History Decision

Shaula should not implement a global clipboard-history recorder for this
feature.

The current GTK clipboard contract exposes the current payload, not the last 50
or 100 entries. Recording global history would require persistent observation
or integration with an external clipboard manager. That introduces:

- privacy risk from passwords, tokens, and private images;
- storage and retention policy requirements;
- compositor- and manager-specific behavior;
- lifecycle issues when clipboard providers disappear;
- a new background-service responsibility unrelated to capture.

The preferred replacement is **Recent Shaula captures**. Shaula can expose a
bounded set of images it created without observing unrelated clipboard content.
The current clipboard remains available for one explicit external image.

Integration with third-party clipboard managers may be explored later as an
optional adapter, not a core dependency.

## 8. Capture and Add

A later v0.1.6 milestone should reduce friction for the navbar/footer scenario:

1. Capture the first region and open Preview.
2. Invoke `Capture and add…`.
3. Complete another Quick or Area capture.
4. Return to the original Preview document.
5. Show the new image in a proposed vertical or horizontal placement.
6. Confirm or adjust the placement.

This requires a capture-result return channel to an existing Preview instance.
It must preserve the capture-session lock contract and must not make Preview a
resident capture daemon.

## 9. Product Non-Goals

The first image-composition release does not include:

- infinite canvas;
- arbitrary page/document authoring;
- rotation or perspective transforms;
- freeform image distortion;
- automatic scrolling capture;
- OCR or semantic page-section detection;
- global clipboard history;
- cloud upload or collaborative editing;
- multi-page documents;
- automatic background removal.

## 10. Delivery Phases

### Phase 1: Structured stitch

- Add images from current clipboard, recent Shaula captures, and files.
- Select and reorder up to four images.
- Vertical and horizontal layouts.
- Gap, alignment, and layout-aware sizing.
- Automatic canvas expansion.
- Editable Image annotations.
- One undo entry and automatic Fit to screen.

### Phase 2: Direct canvas controls

- Expand canvas in four directions.
- Set canvas background.
- Reposition the original capture inside the document.
- Improve free placement and alignment guides.

### Phase 3: Capture and add

- Start a new capture from an existing Preview.
- Return the captured image to the originating document.
- Offer immediate append placement.

### Phase 4: Optional advanced layouts

- Grid layout.
- Equal-size cells.
- Distribution controls.
- Preset comparison templates.

## 11. Product Acceptance Criteria

A first release is usable when:

- a user can create a navbar/footer composition without saving intermediate
  files manually;
- adding images never silently flattens existing editable content;
- cancel produces no document mutation;
- apply produces exactly one undo step;
- undo restores the previous canvas bounds and content;
- redo restores the composition deterministically;
- Copy, Save, Save As, and Done export the full expanded canvas;
- Fit to screen reveals the whole composed document;
- unsupported or oversized inputs produce actionable feedback;
- the feature does not inspect or retain unrelated clipboard history.

## 12. Open Product Questions

- Should recent captures be session-only or persisted across launches?
- Should imported images default to original pixels or normalize to the base
  screenshot scale?
- Should transparent PNG export be supported, or should canvas background always
  be opaque?
- Should the original screenshot remain visually distinguished from imported
  Image annotations?
- Is four images the correct initial limit after real memory measurements?
- Does `Capture and add…` belong in More, the composition sheet, or both?

Technical implications and ownership boundaries are recorded in
`docs/preview-expandable-canvas-design.md`.
