# State

Snapshot for prompt reuse. `./dev state` copies this file, the last 3 commits,
and the working diff.

## Current focus

- The preview toolbar is the active UI surface.
- The goal is to keep the bar compact, useful, and honest about what is real.
- `pin screenshot` is a roadmap item, not a current toolbar action.
- Undo/Redo now has a reusable preview history foundation for document edits.

## Toolbar Actions

- `shaula-copy-symbolic` Copy: implemented. Copies a rendered PNG when the
  preview has modifications, otherwise reuses the original PNG path.
- `shaula-save-symbolic` Save As: implemented. Opens a file chooser and writes
  a PNG to disk.
- `shaula-undo-symbolic` Undo: implemented. Disabled when the history stack has
  no undo entry. Also available with `Ctrl+Z`.
- `shaula-redo-symbolic` Redo: implemented. Disabled when the history stack has
  no redo entry. Also available with `Ctrl+Shift+Z` and `Ctrl+Y`.
- `shaula-share-symbolic` Share: hidden until a backend decision exists.
- `shaula-crop-symbolic` Crop: implemented. It still mutates the current
  preview image internally, but it is now undoable through the preview document
  snapshot history. Direct Crop tool drags apply immediately on mouse release
  and then return to Select mode.
- `shaula-select-symbolic` Select: implemented. Selects and moves annotations.
  The same icon is reused in the overflow menu for Fit to screen and Actual
  size.
- `shaula-arrow-symbolic` Arrow: implemented.
- `shaula-text-symbolic` Text: implemented.
- `shaula-measure-symbolic` Measure: implemented.
- `shaula-rectangle-symbolic` Rectangle: implemented.
- `shaula-highlight-symbolic` Highlight: implemented.
- `shaula-pen-symbolic` Pen: implemented.
- `shaula-more-symbolic` More: implemented overflow menu.
- `shaula-discard-symbolic` Discard: implemented. Closes the preview and
  reports `discard`.

## Overflow Menu

- Fit to screen: implemented.
- Actual size: implemented.
- Reset annotations: implemented.
- Copy path: implemented.
- Open containing folder: implemented.

## Visible Metadata

- Color swatch: implemented.
- Color hex label: implemented.
- Image dimensions label: implemented.
- Zoom label: implemented.

## Preview History

- `ShaulaHistoryStack` lives in `preview_state.*` and stores bounded document
  snapshots with undo/redo arrays and a default capacity of 24 while snapshots
  include full image buffers.
- History tracks state that affects copied/saved output: current image buffer,
  annotations, annotation ids, and modified status.
- History intentionally excludes view-only state: zoom, pan, fit mode, active
  tool, toolbar menu visibility, hover, and transient crop/text drafts.
- Existing wired operations: crop, annotation creation, selected annotation
  move, selected annotation delete, and reset annotations. Annotation moves
  capture before-state on mouse down and commit one history entry on mouse up.
- Reset annotations cancels transient drafts, pushes exactly one pre-clear undo
  snapshot, clears annotations, and relies on the standard edit push to clear
  redo when a new annotation is created after undoing the reset.
- Crop pushes one undo snapshot only after the crop rect validates and a cropped
  pixbuf exists. Remaining annotations are translated to the new image origin;
  annotations outside the crop are removed. In Select mode, clicking Crop with
  a selected rectangle/highlight annotation crops to that rect and removes that
  selected guide annotation from the committed cropped document.
- Restoring history clears transient operations and rebuilds selection from
  cloned annotations to avoid stale pointers.

## Icon Assets Not Wired To The Bar

- `shaula-pin-symbolic` exists in the theme, but there is no current toolbar
  button or callback wired to it.

## Gaps

- Share is hidden until a backend decision exists.
- Pinning is not exposed in the current preview toolbar.
- Redaction and deeper object editing are still future work.
