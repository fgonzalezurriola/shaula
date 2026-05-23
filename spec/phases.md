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
- Preview history, Copy, Save, Save As, and Done/accept polish.
- Shaula-specific default filenames.

## Phase 2: Dev Tools

- Color picker. Passive hover sampling, Tab copy, and swatch-to-active-color
  apply are present; a dedicated eyedropper mode is still future polish.
- Manual ruler / distance measurement.
- Logical vs physical pixel clarity: selection/compositor geometry is logical;
  preview/export/ruler/color/redaction results are physical image pixels.

## Phase 3: Later Surface

- Pretty export polish.
