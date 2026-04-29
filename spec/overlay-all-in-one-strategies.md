# Overlay Backend Decision

Shaula v1 uses one production overlay backend: `gtk4-layer-shell`.

The earlier Raylib and Clay candidates were removed from the product tree. They
did not prove the required Niri/Wayland layer-shell semantics, and keeping them
as selectable runtime strategies added build and QA noise without improving the
current user experience.

## Behavioral Contract

- Best-effort frozen-screen background under a dim layer, with transparent dim fallback.
- Drag-create, move, keyboard nudge, and corner/edge resize interactions.
- Corner and edge resize handles.
- Dimension badge near the selection.
- Floating toolbar with only real actions (`Capture`, `Cancel`).
- Toolbar placement policy: prefer below selection, then above, then nearest visible edge.
- Toolbar placement remains inside visible output, avoids handle overlap and badge overlap when possible, and applies anti-jitter thresholding.
- Confirmed capture persists the last valid toolbar position.
- Runtime unavailability must emit deterministic `ERR_OVERLAY_UNAVAILABLE`.
- Interaction timeout must emit deterministic `ERR_OVERLAY_TIMEOUT`.

## Backend Contract

`gtk4-layer-shell` is the production backend because it provides compositor-native
layer-shell behavior on Wayland/Niri:

- overlay layer placement,
- exclusive keyboard interactivity,
- monitor targeting,
- pointer input over the overlay surface,
- GTK drawing for selection chrome,
- frozen background preview when `grim` can prepare the visual image.

There is no public overlay backend selector in v1.

## QA Command

```bash
bash scripts/qa/benchmark-overlay-strategies.sh
```

The script now records GTK readiness evidence only. Intrusive timing lanes may
be enabled with:

```bash
SHAULA_QA_ALLOW_INTRUSIVE_UI=1 bash scripts/qa/benchmark-overlay-strategies.sh
```

Generated evidence:

- `.qa/evidence/task-18-overlay-strategy-compare.json`
