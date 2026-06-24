# Preview UI Contract

This document owns preview toolbar layout, overflow, startup readiness, and selection-chrome constraints.

## Stable Metadata Width

Preview metadata that changes during interaction must reserve stable width.

- The color hex label uses a fixed code-style width.
- The zoom percentage uses a fixed code-style width.
- Hover sampling and zoom changes must not shift the toolbar.

## Toolbar Packing

Primary preview actions are packed at the headerbar start rather than installed as a centered title widget. This leaves left-side room for tools before they overflow into the More menu.

Overflow decisions use the measured gap between the toolbar start and the right-side metadata group. Do not use the full headerbar width: that can reveal secondary buttons too early and temporarily overlap color, dimension, and zoom labels.

Contextual Select actions and tool property controls live in floating canvas HUDs. Changing tools, selecting annotations, or selecting regions must not change the preview window's natural titlebar width or ask Niri to resize a floating preview.

## Stable Overflow Width

The toolbar action group keeps a stable requested width.

- Hiding or revealing buttons must not change the headerbar natural width.
- Tool changes must not resize the floating preview.
- The pre-present overflow pass uses the intended default window width.
- Post-present resize handling may use measured headerbar-to-metadata bounds only after GTK has produced a sane allocation.

Supported preview size presets:

- 900x600
- 1100x720
- 1400x900

The 900x600 preset is expected to fit the complete primary toolbar. Overflow thresholds must be based on measured compact button widths, not conservative titlebar estimates.

## Startup Readiness

The preview is presented only after its initial visual state is complete:

1. Register the custom icon theme.
2. Build the complete primary toolbar.
3. Reserve metadata widths.
4. Resolve the configured/default window size.
5. Compute the initial fit zoom.
6. Apply the initial overflow layout.
7. Call `gtk_window_present()`.

After presentation, toolbar updates may change sensitivity, active state, or menu state. They must not add new primary toolbar buttons or expand the titlebar natural width.

## Selection Chrome

Selected Rectangle annotations draw an external selection outline derived from `data.rectangle.rect`, not from the broad-phase annotation bounds. Padding remains stable in screen pixels so selection chrome stays aligned across zoom levels. The real rectangle stroke is repainted above the outline so dashed content remains visible. Rectangle keeps eight square resize handles.

Singly selected Image and committed Text annotations keep their existing selection boxes and add four square corner handles. Handle drawing and hit targets remain stable in screen pixels. Image corner resize preserves aspect ratio, keeps the opposite corner fixed, prevents flipping, and changes only `data.image.rect`. Text corner resize changes `font_size` uniformly and corrects `data.text.position` after Pango recomputation so the opposite visual corner remains fixed.

Per-object handles are single-selection only. Multi-selection draws one group box without resize handles. Multi-selection resize, rotation, flipping, freeform Image distortion, and Text wrapping or fixed-width boxes remain out of scope.

## Tool Placement

Pan, Crop, and Annotation Eraser are fixed navigation/utility tools after Copy, Save, Undo, and Redo.

Numbered canvas tools:

| Key | Tool |
| --- | --- |
| `0` | Annotation Eraser |
| `1` | Select |
| `2` | Rectangle |
| `3` | Arrow |
| `4` | Line |
| `5` | Text |
| `6` | Pen |
| `7` | Highlight |
| `8` | Measure |
| `9` | Spotlight |

Only implemented numbered tools display GTK keycap badges.

Fit to screen, Actual size, and Reset annotations are overflow utility actions. Save As and Paste text/image remain More-menu actions. The paste row uses `shaula-paste-symbolic` and the same icon-label layout as the other actions. `Ctrl+Shift+V` is exposed in its tooltip rather than as inline row chrome. The action does not add a permanent toolbar button or affect the headerbar's natural width.
