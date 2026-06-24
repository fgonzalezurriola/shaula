# Shaula Roadmap

Shaula is a focused Wayland screenshot workflow with a native capture overlay
and post-capture preview/editor.

## Released: v0.1.5

v0.1.5 was released on 2026-06-24 after manual validation of the primary
Wayland/Niri journeys. It delivers system-clipboard text/image paste, Image and
Text resize, Annotation Eraser, stronger selection and history behavior, direct
Quick/Area copy and save shortcuts, expanded runtime discovery, and removal of
the resident-daemon architecture.

Detailed notes live in `docs/release-v0.1.5.md`.

## Current Release Focus: v0.1.6

v0.1.6 is the **image composition and expandable canvas release**.

The release has no fixed date. It should ship when the new document model and
human workflow are coherent, bounded, and manually validated. The active plan
lives in `docs/plan-v0.1.6.md`.

### Core direction

- Separate bounded document canvas dimensions from the original screenshot
  pixbuf.
- Make canvas expansion undoable and deterministic.
- Compose a small ordered set of images vertically or horizontally.
- Import images from the current clipboard and explicit files.
- Keep imported images editable instead of flattening immediately.
- Preserve Copy, Save, Save As, Done, crop, Spotlight, selection, resize, and
  export across expanded documents.
- Keep one composition apply as one undo step.
- Refit the view after document-size changes.
- Add explicit canvas dimension, total-pixel, and memory limits.
- Keep global clipboard history out of the core product.

### Planned additions

- Recent Shaula captures as a composition source.
- Direct controls for adding canvas space above, below, left, or right.
- Canvas background controls.
- `Capture and add…` from an existing Preview.
- Grid and equal-cell comparison layouts.
- Simple alignment and distribution guidance for free placement.

These additions must not appear as placeholders. Each one enters the public UI
only when its source loading, cancellation, history, export, and failure paths
are complete.

### Emergent work

v0.1.6 may also include work discovered during implementation when it:

- fixes a bug exposed by the new document model;
- removes a confusing capture or Preview interaction;
- reduces memory, rendering, history, or ownership risk;
- improves module boundaries or testability needed by the release;
- aligns Settings, installation, integration, or documentation;
- addresses a concrete issue found during normal daily use.

This lane keeps the plan flexible without turning v0.1.6 into an unrelated
feature collection.

### Existing workflow quality

The stable v0.1.5 baseline remains part of the release bar:

- Quick, Area, Fullscreen, All Screens, and Window capture must stay predictable.
- Copy, Save, Save As, Done, and Discard must keep their documented semantics.
- Preview toolbar width and overflow must remain stable.
- Keyboard shortcuts must not conflict with text editing, modal tools, or active
  selections.
- Settings must preserve custom values during unrelated saves.
- Noctalia remains optional and outside the capture hot path.
- Errors and notifications must remain actionable without exposing temporary
  runtime artifacts.

### v0.1.6 release bar

v0.1.6 is ready when:

- vertical and horizontal composition solve the navbar/footer and comparison
  workflows cleanly;
- undo and redo restore canvas bounds, assets, and pixels deterministically;
- Copy, Save, Save As, Done, crop, and Spotlight operate on the expanded canvas;
- cancel and failure paths leave the document unchanged;
- supported image and history limits keep memory bounded;
- no known high-impact regression affects the v0.1.5 capture or Preview flows;
- automated checks pass;
- the final candidate receives manual Wayland/Niri validation.

Required automated baseline:

```bash
./dev check
git diff --check
```

After UI changes:

```bash
./dev dev-install --yes
```

Detailed product and technical contracts:

- `docs/plan-v0.1.6.md`
- `docs/plan-image-composition.md`
- `docs/preview-expandable-canvas-design.md`

## Shipped Foundation

- Native Wayland capture overlay and GTK preview/editor.
- Quick, Area, Fullscreen, All Screens, Window, and Previous Area capture paths.
- Direct CLI orchestration with short-lived helpers and no resident daemon.
- Niri direct capture, wlroots/`grim`, and xdg-desktop-portal runtime paths.
- Noctalia Bar Widget with packaged installer integration.
- Copy, Save, Save As, Done, Discard, and actionable save notifications.
- Undo/redo history for document edits.
- Multi-annotation selection, clipboard, duplicate, move, and delete.
- Explicit system-clipboard paste for text and image annotations.
- Rectangle, Arrow, Line, Text, Pen, Highlight, Measure, Spotlight, Crop,
  Blur, pixel Erase, and Annotation Eraser workflows.
- Passive color sampler with Tab copy and swatch-to-active-color apply.
- Native Settings UI, setup wizard, release installer, and AUR packaging.

## After v0.1.6

Potential follow-up work should continue to be driven by real usage:

- Dedicated active eyedropper mode if passive sampling and swatch apply are not
  sufficient.
- More filename templating and save-path configuration.
- Additional preview history affordances if users need more visibility or
  control.
- General QuickShell integration only after the draft architecture and expected
  user value are reviewed.
- Deploy and maintain the static product landing page.
- Composition refinements that do not fit coherently into the v0.1.6 release.

## Current Non-Goals

These are not targets for v0.1.6:

- exposing placeholders for unfinished features;
- infinite canvas;
- OCR;
- scrolling capture;
- screen recording;
- deep or semantic redaction;
- AI removal or automatic background removal;
- smart selection;
- global clipboard history;
- multi-page documents;
- Share/upload backend or collaboration;
- Pin action/window persistence;
- perspective transforms or freeform image distortion;
- turning Preview into a general-purpose design application.
