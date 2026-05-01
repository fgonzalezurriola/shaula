# Shaula Delivery Phases

See [spec/requirements.md](requirements.md) for product direction and [spec/performance.md](performance.md) for the numeric budgets.

This file is sequencing only. Nothing here is public contract until the behavior is implemented and mapped to deterministic `ERR_*` outcomes.

## Phase 0: Solid Capture Core

- Area, fullscreen, focused output, and focused window capture.
- Overlay selection with handles, aspect lock, Esc cancel, Enter confirm, and repeat previous area.
- Copy/save output and file-first configuration.

## Phase 1: Useful After Capture

- Floating post-capture preview.
- Crop and basic redaction.
- Pin screenshot when the compositor allows it.
- History and file naming polish.

## Phase 2: Dev Tools

- Color picker.
- Manual ruler / distance measurement.
- Logical vs physical pixel clarity.
- OCR and QR only if the optional path stays clean.

## Phase 3: Later Surface

- Smart selection.
- Remove-object style editing.
- Combine screenshots.
- Scrolling capture when the Wayland/Niri strategy is stable.
