# State

Snapshot for prompt reuse. `./dev state` copies this file, the last 3 commits,
and the working diff.

## Current focus

- The preview toolbar is the active UI surface.
- The goal is to keep the bar compact, useful, and honest about what is real.
- `pin screenshot` is a roadmap item, not a current toolbar action.

## Toolbar Actions

- `shaula-copy-symbolic` Copy: implemented. Copies a rendered PNG when the
  preview has modifications, otherwise reuses the original PNG path.
- `shaula-save-symbolic` Save As: implemented. Opens a file chooser and writes
  a PNG to disk.
- `shaula-share-symbolic` Share: present but disabled. No backend decision yet.
- `shaula-crop-symbolic` Crop: implemented. Destructively crops the current
  preview image.
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

## Icon Assets Not Wired To The Bar

- `shaula-pin-symbolic` exists in the theme, but there is no current toolbar
  button or callback wired to it.

## Gaps

- Share is still a disabled placeholder.
- Pinning is not exposed in the current preview toolbar.
- Redaction and deeper object editing are still future work.
