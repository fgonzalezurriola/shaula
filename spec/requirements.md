# Shaula Product Direction

Shaula is a Wayland-first, Niri-first screenshot tool written in Zig. It should feel fast in the hand and small on the system. The hot path must stay inside the budgets in [spec/performance.md](performance.md), and the default footprint must remain modest enough that capture feels instant instead of heavy.

Shottr is the behavior benchmark, not the platform model. The goal is to copy the useful characteristics of Shottr that make a screenshot tool pleasant to use: precise selection, low-friction capture, a fast post-capture path, and practical tools for developers and documentation work. macOS-only APIs, Apple Silicon assumptions, and desktop-specific integrations do not belong in Shaula's core scope.

## Product Principles

- Wayland-first and Niri-first by default.
- CLI-first, file-first, and deterministic.
- The capture path stays short; preview and editing are downstream work.
- Every machine-visible failure maps to a stable `ERR_*` token.
- Product polish comes from precision, not decorative UI.

## Shottr Characteristics We Want

### Capture

- Area capture.
- Fullscreen capture.
- Focused output capture.
- Focused window capture through Niri IPC.
- Repeat previous area.

### Selection UX

- Fullscreen overlay.
- Resize handles.
- Move the active selection.
- Aspect-ratio locking.
- Visible `x y width height` feedback.
- Magnifier during selection.
- `Esc` cancels.
- `Enter` confirms.

### Post-Capture Flow

- Floating preview after capture.
- Copy, save, and discard actions.
- Crop before export.

### Editing and Redaction

- Pixelate, blur, or solid-bar redaction first.
- Rectangles and arrows next.
- Text, highlight, and free draw after the core path is stable.

### Dev Tools

- Color picker.
- Manual ruler / distance measurement.
- Logical vs physical pixel awareness: compositor/selection geometry is
  logical-output based, while PNG dimensions, preview sampling, ruler
  measurements, redaction edits, and exported pixels are physical image pixels
  after output-scale normalization.
- Average-area color if it stays cheap and deterministic.

### Clipboard, Export, and Config

- Copy PNG to clipboard.
- Save PNG to disk.
- Copy the saved path.
- Default save folder and filename template. Current default names are
  `YYYYMMDD-HHMMSS.png` for preview save/accept flows and direct saved
  captures, with `-2`, `-3`, and so on for collisions.
- TOML configuration.
- Post-capture action defaults.

## Priority Slices

| Slice | Focus |
| --- | --- |
| v0 | Area/fullscreen/focused output/window capture, repeat previous area, overlay selection, Esc/Enter, copy/save, TOML config |
| v1 | Floating preview, crop, pixelate/redaction, arrows/rectangles, history, file naming |
| v2 | Color picker, ruler, pretty export |

## Explicit Non-Goals

- Screen recording.
- OCR and QR.
- Scrolling capture.
- Deep redaction.
- AI removal and object removal.
- Smart selection.
- Combine screenshots.
- Share/upload backend.
- Pin action/window persistence.
- macOS-specific APIs or workflows.
- Placeholder future UI surfaces.
