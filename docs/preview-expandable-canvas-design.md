# Preview Expandable Canvas Design

> Status: **Active technical design** — Implementation details remain subject to ADRs
>
> Product plan: `docs/plan-image-composition.md`
>
> Historical release-number plan: `docs/plan-v0.1.6.md`
>
> Release scope: **Post-v0.1.6; no version assigned**

## 1. Problem Statement

Preview currently treats the decoded screenshot pixbuf as both the base image
and the complete document bounds. This is appropriate for annotation editing but
prevents imported images from extending beyond the original capture.

Image composition requires document bounds that can differ from the original
capture dimensions. The canvas size, base-image placement, imported image
placement, history, rendering, export, and geometry limits must therefore become
explicit document contracts.

This is not a request to introduce an infinite canvas. The target is a bounded,
exportable raster document whose dimensions may be expanded by deliberate user
actions.

## 2. Current Constraints

### 2.1 Document dimensions

`ShaulaPreviewDocument` stores one `GdkPixbuf *image`. Its width and height are
returned directly by `shaula_preview_document_width()` and
`shaula_preview_document_height()`.

The pixbuf currently serves three roles:

- original capture pixels;
- document dimensions;
- coordinate bounds for editing and export.

Those roles must be separated.

### 2.2 Geometry and input

Preview canvas, gesture, clipboard-placement, Spotlight, and crop paths call
`shaula_preview_image_width()` and `shaula_preview_image_height()` as their hard
bounds. Image insertion and resize clamp imported Image annotations to the base
screenshot.

Changing only paste placement would produce inconsistent behavior because later
movement, resize, selection, crop, and render paths would still use the old
bounds.

### 2.3 History

Preview snapshots currently retain:

- the base pixbuf;
- annotations;
- Spotlight regions;
- next annotation ID;
- modified state.

Canvas dimensions and base-image placement do not exist independently, so undo
cannot currently restore a canvas expansion.

### 2.4 Rendering and export

Rendering allocates output using the current image width and height, then draws
the base pixbuf, Spotlight effects, and annotations. Copy, Save, Save As, and Done
must all use the same expanded document bounds once composition exists.

## 3. Target Document Model

A future document should separate the bounded canvas from image assets and their
placement.

Conceptual model:

```text
ShaulaPreviewDocument
├── canvas
│   ├── width
│   ├── height
│   └── background
├── base_image
│   ├── asset
│   └── rect
├── annotations
│   ├── Image annotations referencing assets
│   └── vector/text annotations
├── spotlight_regions
└── history
```

Suggested C-level direction:

```c
typedef struct {
  int width;
  int height;
  ShaulaColor background;
  gboolean background_opaque;
} ShaulaCanvas;

typedef struct {
  GdkPixbuf *pixels;
  int width;
  int height;
} ShaulaImageAsset;

typedef struct {
  ShaulaImageAsset *asset;
  ShaulaRect rect;
} ShaulaBaseImage;
```

The exact public types should be chosen during implementation. The durable
contract is that canvas dimensions and base-image placement are independent of
the captured pixbuf dimensions.

## 4. Coordinate Contract

### 4.1 Initial implementation

The smallest safe implementation may keep the canvas origin at `(0, 0)` and
expand only toward the right and bottom. This reduces geometry churn but limits
direct canvas controls.

### 4.2 Full implementation

Supporting expansion on all four sides requires one of these models:

- shift every output-affecting object when space is inserted above or left; or
- store a document-space origin and map it to raster-export coordinates.

Explicitly shifting all content during one undoable resize operation is easier
to reason about with the current image-coordinate model. A persistent negative
origin would affect more hit-testing, rendering, and serialization paths.

Recommended contract:

- exported canvas always spans `(0, 0)` to `(width, height)`;
- adding space above or left translates base-image placement, annotations, and
  Spotlight regions by the inserted amount;
- the translation and canvas resize form one document mutation and one history
  entry.

## 5. Base Image Contract

The original capture should become a placed base layer rather than an implicit
full-canvas bitmap.

Required behavior:

- it renders below Spotlight effects and annotations;
- its default rect is `(0, 0, captured_width, captured_height)`;
- expanding canvas does not resample it;
- direct composition does not silently flatten it with imported images;
- crop may replace or reposition it according to the final crop contract;
- reset annotations must not reset canvas size or base-image placement unless a
  separate reset-document action is defined.

Whether the base image becomes directly selectable is an open design question.
Keeping it non-selectable preserves current Preview semantics. Direct canvas
controls can still reposition it as part of an explicit document operation.

## 6. Image Asset Ownership

Current Image annotations own decoded pixel payloads. Large compositions make
history cost a first-order concern.

Snapshots should not deep-copy immutable image pixels. Use shared, reference-
counted image assets and copy only output-affecting placement metadata.

Required asset properties:

- immutable pixel payload after decode;
- explicit reference counting;
- dimensions cached with the asset;
- one owner contract across document, annotations, clipboard clones, history,
  undo/redo, and destruction;
- no dependency on the system clipboard provider after insertion.

This preserves the accepted clipboard ownership rule while avoiding repeated
pixel copies across up to 24 history snapshots.

## 7. Canvas Resize Mutation

Canvas expansion should be owned by the document-edit layer, not performed ad
hoc by toolbar or clipboard code.

A resize operation needs:

- target width and height;
- optional content translation;
- background behavior;
- validation against dimension and pixel limits;
- one pending history snapshot;
- deterministic success or error feedback;
- selection and transient-region reconciliation;
- view refit requested only after a successful mutation.

Conceptual API:

```c
typedef struct {
  int width;
  int height;
  double translate_x;
  double translate_y;
} ShaulaCanvasResize;

gboolean shaula_preview_document_resize_canvas(
    ShaulaPreviewState *state,
    ShaulaCanvasResize resize);
```

The final API should follow existing preview document-edit and command ownership.

## 8. Composition Operation

A structured composition should be calculated before mutating the live document.

Input:

- ordered image assets;
- layout: vertical or horizontal;
- gap;
- alignment;
- sizing mode;
- current document bounds.

Output plan:

- required new canvas bounds;
- destination rect for each image;
- optional translation for existing content;
- selection IDs for inserted annotations.

Apply contract:

1. Validate all assets and total output limits.
2. Build the complete placement plan without mutating state.
3. Push one document snapshot.
4. Resize the canvas.
5. Insert all Image annotations with new IDs.
6. Select only the inserted set.
7. Clear incompatible transient region or gesture state.
8. Mark the document modified.
9. Update command sensitivity and metadata.
10. Refit the view.

Any failure before step 3 leaves the document unchanged. Failures after mutation
begins should restore the snapshot rather than leave a partial composition.

## 9. History Contract

Snapshots must include every output-affecting canvas property:

- canvas width and height;
- canvas background;
- base-image rect;
- shared base-image asset reference;
- annotations and their shared image asset references;
- Spotlight regions;
- next annotation ID;
- modified state.

View zoom, pan, sheet state, thumbnail selection, and proposed layout remain
transient UI state and stay outside document history.

A composition apply is one undo entry regardless of image count. Undo restores
both previous bounds and previous objects. Redo must reproduce the same pixels
and geometry without rereading files or clipboard content.

## 10. Geometry Audit

Every use of screenshot dimensions must be classified as one of:

- canvas bounds;
- base-image pixel bounds;
- visible viewport bounds;
- annotation asset bounds.

Known areas requiring audit:

- `preview_state.c` dimension helpers;
- `preview_canvas.c` draw and point clamping;
- `preview_gesture.c` movement and resize constraints;
- `preview_system_clipboard.c` insertion placement;
- `preview_paste_placement.*`;
- `preview_spotlight.c` full-document dimming;
- crop and region effects;
- selection and marquee intersection;
- toolbar dimension metadata;
- native Preview initial fit calculation;
- render/export allocation;
- annotation duplication and internal paste cascade.

Do not preserve ambiguous helper names such as `image_width` when callers need
canvas width. Introduce explicit vocabulary before changing behavior.

## 11. Rendering Order

Recommended render order:

1. Allocate canvas-sized output surface.
2. Paint canvas background or clear transparency.
3. Render the placed base image.
4. Render Spotlight document effects.
5. Render annotations, including imported Image annotations.
6. Exclude selection chrome, HUDs, and transient composition preview.

This preserves the current rule that Spotlight is a document effect below
annotations. Imported images remain annotations and therefore render above
Spotlight unless the product later introduces explicit layer ordering.

A future composition feature should not add general layer-panel semantics in its
first release.

## 12. Crop Semantics

Current crop changes the effective screenshot document. With an expanded canvas,
crop should operate on the complete composed document.

Recommended behavior:

- crop rectangle is expressed in canvas coordinates;
- output canvas becomes the crop size;
- base-image rect, annotations, and Spotlight regions are translated by the
  negative crop origin;
- content outside the crop is clipped or removed according to existing type
  semantics;
- the operation remains one undo entry.

This requires tests for base-image partial intersection and imported Image
annotations crossing crop edges.

## 13. Spotlight Semantics

Spotlight currently dims the full screenshot bounds. After canvas expansion it
must dim the full canvas bounds.

Existing Spotlight regions remain in canvas coordinates. Adding space above or
left translates them with the rest of document content. Adding space right or
below does not alter their stored rectangles.

## 14. Clipboard and Recent Captures

The current system-clipboard reader should continue to read one current payload
per explicit request. Image composition may reuse its decode and validation
boundaries but must not weaken cancellation, clipboard-change, weak-window, or
image-ownership contracts.

Recent Shaula captures require a separate source abstraction. It should expose
bounded capture metadata and resolvable image assets without turning runtime
artifact paths into durable public paths or notification targets.

Questions to resolve before implementation:

- session-only versus persisted recents;
- whether temporary runtime captures remain available after their originating
  Preview closes;
- cleanup ownership;
- maximum item count and byte budget;
- behavior when a recent item has already been deleted.

## 15. Limits and Error Taxonomy

Composition requires explicit limits before allocating output surfaces.

Candidate limits to validate with measurements:

- maximum four imported images per operation;
- maximum 16,384 pixels per side, aligned with current pasted-image validation;
- maximum total canvas pixels;
- maximum decoded asset pixels;
- maximum recent-capture byte budget.

UI feedback may remain human-readable within Preview. Any future public CLI or
JSON composition surface must use deterministic `ERR_*` outcomes owned by its
boundary module rather than parsing GTK feedback strings.

Likely error categories:

- invalid image;
- unsupported image format;
- image too large;
- canvas too large;
- decode failure;
- source disappeared;
- insufficient memory;
- composition plan invalid.

## 16. Command and Module Ownership

Suggested boundaries:

- Preview Commands owns action availability, shortcut, and execution routing.
- A composition controller owns sheet lifecycle and source loading.
- A pure placement module calculates deterministic layout geometry.
- Preview document edit owns canvas resize and atomic apply/rollback.
- Annotation editor owns inserted Image IDs and final selection synchronization.
- Render owns canvas-sized output composition.
- Recent-capture storage, if added, belongs outside Preview UI widgets.

The GTK sheet must not mutate `ShaulaPreviewDocument` directly.

## 17. UI and View State

Applying a composition can greatly change aspect ratio. After success:

- request Fit to screen;
- preserve stable toolbar natural width;
- update dimension metadata without shifting reserved metadata layout;
- keep selection HUDs on the canvas overlay;
- avoid presenting the window before the composition sheet is ready;
- do not close Preview.

Previewing a proposed composition should use transient overlay rendering or a
small independent preview. It must not create history entries while users
reorder thumbnails or change gap/alignment controls.

## 18. Test Strategy

Unit coverage should include:

- vertical and horizontal placement geometry;
- gap and alignment calculations;
- canvas expansion bounds;
- left/top translation if supported;
- one-entry undo/redo restoration;
- asset lifetime across undo, redo, duplicate, crop, and destruction;
- export dimensions and pixel placement;
- Spotlight over expanded canvas;
- crop of composed content;
- cancellation with no mutation;
- invalid or oversized asset rejection before history mutation;
- deterministic ordering and selection IDs.

Manual Wayland checks should include:

- composing two live captures;
- composing current clipboard image plus a Shaula capture;
- large vertical result requiring Fit to screen;
- saving and copying an expanded canvas;
- closing Preview during asynchronous source loading;
- undo/redo after moving and resizing composed images.

Required repository verification after implementation changes remains:

```bash
./dev check
git diff --check
```

UI implementation also requires:

```bash
./dev install
```

## 19. Implementation Sequence

Recommended order:

1. Introduce explicit canvas dimensions without changing visible behavior.
2. Rename dimension helpers so canvas and asset bounds cannot be confused.
3. Add base-image placement and preserve current `(0, 0)` behavior.
4. Move history snapshots to shared immutable image assets.
5. Add atomic canvas resize and translation.
6. Update rendering, export, crop, Spotlight, input, and metadata contracts.
7. Add pure vertical/horizontal composition planning.
8. Add the composition sheet and explicit image sources.
9. Add recent Shaula captures after cleanup and privacy contracts are resolved.
10. Evaluate `Capture and add…` only after the base composition workflow is
    stable.

## 20. Decisions Required Before Coding

- Canvas background: opaque by default or transparent-capable?
- Base image: permanently non-selectable or selectable in a dedicated mode?
- Expansion directions in the first release: right/bottom only or all four?
- Asset implementation: custom ref-counted type or `GObject` wrapper?
- Recent capture lifetime and persistence policy.
- Total canvas pixel and memory budgets.
- Whether crop clips partially intersecting Image annotations or rasterizes them.
- Whether imported images always render above Spotlight.

These decisions are durable enough to warrant an ADR when implementation begins.
This file guides post-v0.1.6 implementation, but unresolved alternatives must not be
treated as shipped architecture until code, tests, and the relevant ADR settle
them.
