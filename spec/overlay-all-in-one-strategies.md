# Overlay All-in-one Strategy Notes

This document describes expected behavior and comparison criteria for the three isolated overlay-helper strategies:

1. `gtk4-layer-shell`
2. `raylib+clay` (alias `raylib-clay`)
3. `raylib`

## Shared behavioral contract

- Best-effort frozen-screen background under a dim layer, with transparent dim fallback.
- Drag-create, move, keyboard nudge, and corner/edge resize interactions.
- Corner and edge resize handles.
- Dimension badge near the selection.
- Floating toolbar with only real actions (`Capture`, `Cancel`).
- Toolbar placement policy: prefer below selection, then above, then nearest visible edge.
- Toolbar placement remains inside visible output, avoids handle overlap and badge overlap when possible, and applies anti-jitter thresholding.
- Confirmed capture persists the last valid toolbar position.
- Strategy/runtime unavailability must emit deterministic `ERR_OVERLAY_UNAVAILABLE`.
- Interaction timeout must emit deterministic `ERR_OVERLAY_TIMEOUT`.

## Comparison metrics

`scripts/qa/benchmark-overlay-strategies.sh` emits comparison evidence without
requiring intrusive UI by default. Intrusive timing lanes may be enabled with
`SHAULA_QA_ALLOW_INTRUSIVE_UI=1`.

- `first_paint_ms`
- `interactive_ms`
- `drag_resize_stability_pct`
- `toolbar_quality_pct`
- `toolbar_repositions`
- `supports_layer_shell`
- `supports_frozen_background`
- `maintainability_note`
- deterministic `error_code` when unavailable

## Expected constraints by strategy

### gtk4-layer-shell

- Uses compositor-native layer shell semantics through GTK4 + Gtk4LayerShell.
- Most direct path for Wayland overlay behavior when runtime deps are available.
- Current production strategy for CleanShot-like selection UX.
- Supports frozen background when `grim` can prepare the visual preview.
- Fails with `ERR_OVERLAY_UNAVAILABLE` when GTK/GI/layer-shell runtime is missing or unsupported.

### raylib+clay

- Keeps independent render/input route with Raylib loop and Clay lifecycle.
- Requires both Raylib and Clay real dependencies (no stub modules).
- Must prove layer-shell-equivalent Wayland input behavior before production promotion.
- Fails with `ERR_OVERLAY_UNAVAILABLE` when either dependency is not wired.

### raylib

- Keeps independent render/input route with Raylib-only primitives.
- Requires real Raylib dependency.
- Must prove layer-shell-equivalent Wayland input behavior before production promotion.
- Fails with `ERR_OVERLAY_UNAVAILABLE` when Raylib is not wired.

## Production decision rule

`auto` resolves to the current production helper path. Keep `gtk4-layer-shell`
as production while Raylib candidates cannot prove true Wayland overlay/input
semantics without compositor-specific hacks. Raylib can replace GTK only after
the comparison evidence shows:

- layer-shell-equivalent pointer and keyboard behavior,
- frozen background rendering,
- unchanged helper stdout envelope v1,
- first-paint within 15ms of GTK,
- no regression in drag/resize stability or toolbar quality.

## Practical comparison command

```bash
bash scripts/qa/benchmark-overlay-strategies.sh
```

Generates:

- `.qa/evidence/task-18-overlay-strategy-compare.json`
