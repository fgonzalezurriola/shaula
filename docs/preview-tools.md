# Preview Tools

This document owns the implemented preview action and tool contracts.

## Primary and Utility Actions

### Copy

`shaula-copy-symbolic` copies a rendered PNG when the preview has modifications. Otherwise it reuses the original PNG path.

### Save

`shaula-save-symbolic` and `Ctrl+S` save a new timestamped PNG under `~/Pictures/shaula/YYYYMMDD-HHMMSS.png`, adding a numeric suffix on collisions and falling back to `~/shaula` when the Pictures directory cannot be created or written.

Quick Save updates preview save metadata but does not create undo history. After a successful save, it closes the preview only when the close-on-save setting is enabled.

### Save As

Save As is available from the responsive utility/More menu and through `Ctrl+Shift+S`. It opens a file chooser, writes a PNG, and updates preview save metadata. A later `Ctrl+S` still creates a new timestamped version.

### Undo and Redo

- `shaula-undo-symbolic`: `Ctrl+Z`; disabled without an undo entry.
- `shaula-redo-symbolic`: `Ctrl+Shift+Z` or `Ctrl+Y`; disabled without a redo entry.

### Discard

`shaula-discard-symbolic` closes the preview and reports the `discard` action.

### Hidden/unsupported actions

- Share is not exposed; no upload backend exists.
- Pin is not exposed. Unknown legacy helper action strings remain tolerated by the preview-result parser for compatibility but are not public actions.

## Tool Shortcuts

Shaula keeps its numeric toolbar slots while using Excalidraw-style mnemonic
letters for the drawing tools that have direct equivalents:

| Tool | Shortcut |
| --- | --- |
| Select | `V` or `1` |
| Rectangle | `R` or `2` |
| Arrow | `A` or `3` |
| Line | `L` or `4` |
| Text | `T` or `5` |
| Pen | `P`, `X`, or `6` |
| Highlight | `H` or `7` |
| Measure | `8` |
| Spotlight | `9` |
| Annotation Eraser | `E` or `0` |

Letter shortcuts activate tools only when pressed without modifiers. Modified
forms such as `Shift+V` and `Shift+H` remain available for future actions.
Holding `Space` temporarily enters Hand/Pan and restores the previous tool on
release. Crop and contextual Blur have no letter shortcut. `F` remains the
Fit-to-screen shortcut.

Toolbar badges show the numeric slot. Tooltips and responsive More-menu rows
show only the mnemonic letter when one exists; numeric-only tools keep the plain
tool name because the badge already exposes their slot.

## Selection Model

`shaula-select-symbolic` supports:

- clicking an annotation to select it;
- dragging visible selected annotation geometry or a selection-box edge to move it;
- dragging empty image space to create a temporary rectangular region;
- clearing selection by clicking or dragging outside the image;
- middle-button canvas panning.

Selected annotation actions appear only while Select is active and an annotation selection exists.

Temporary region selections are not annotations, are not exported, and do not enter undo history by themselves. They expose contextual Crop, Blur, pixel Erase, and Spotlight actions.

Annotation multi-select supports click-only selection, Shift+click toggle, marquee intersection, select-all, batch duplication, batch deletion, and moving the selected set as one undoable gesture. A successful marquee clears its temporary region rectangle after selecting objects. A drag with no annotation matches remains a contextual region. Multi-selection suppresses all per-object selection boxes and handles, then draws one group bounding box around the selected set.

Selection-box movement applies to Pen, Highlight, Arrow/Line, Text, Measure, Rectangle, Image, and the multi-selection group box. Only the four border segments are move targets; empty box interiors remain transparent to hit testing so visible objects behind them can still be selected. Edge and handle tolerances are fixed at 8 screen pixels. The edge target is computed from the rendered selection frame in screen coordinates for both hover and drag begin. Input priority is resize/curvature handles, selection-box edges, visible annotation geometry, then empty-space marquee. Edge drags preserve the current single or grouped selection, use `grab`/`grabbing` cursors, and commit movement as one history gesture. A press and release without movement does not collapse or otherwise change the selection. Resize and curvature can begin only from handles already visible on the current single selection; selecting an Arrow or Line near its midpoint must not bend it. Rectangle keeps eight box handles, while Image and committed Text expose only four corner handles. Per-object handles remain single-selection only; multi-selection resize is not supported.

## Crop

`shaula-crop-symbolic` is undoable through preview document history.

- Direct Crop tool drags apply on mouse release and return to Select.
- Contextual Crop can use a temporary region.
- `SHAULA_PREVIEW_COMMAND_CROP_SELECTED` crops to a selected Rectangle annotation.

## Hand/Pan

`shaula-hand-symbolic` is view-only.

- Routed through `SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND`.
- Left-drag pans while active.
- Cursor states are `grab` and `grabbing`.
- It does not edit pixels, annotations, Spotlight regions, modified/save state, or undo history.
- Holding Space temporarily switches to Hand/Pan unless an editable widget is focused.
- Releasing Space restores the previous tool after any active pan finishes.

## Spotlight

`shaula-spotlight-symbolic` is an independent primary tool and a contextual region action.

Direct Spotlight creation:

1. Drag a region on the canvas.
2. Commit the Spotlight on release.
3. Open the floating properties HUD.

Spotlight regions are vector document effects in image coordinates. They are not routed through raster crop/blur/erase helpers. Preview and export compose Spotlight effects before drawing annotations so later text and vector marks remain visible above the dimming.

Direct Spotlight uses `drag_start_image` and `drag_current_image`, not `region_selection_rect`. This prevents multiple or nested Spotlights from inheriting stale Select-region geometry.

The Spotlight HUD is attached to the preview `GtkOverlay` and state-owned by `ShaulaPropertiesHudState`. It exposes:

- Back;
- border color;
- border width;
- pointed corners;
- rounded corners.

The HUD targets the just-created Spotlight by `spotlight_index`. Changes update that entry reactively and become defaults for the next Spotlight. Closing/back clears the active target. HUD state is UI/config state and is excluded from undo snapshots.

The Spotlight icon is the filter-style glyph. Do not reuse it for Highlight.

## Annotation Duplicate and Clipboard

The selection HUD uses the copy glyph (`shaula-copy-symbolic`) for its Duplicate action. The button and `Ctrl+D` duplicate the selected annotation set through the multi-object paste path without replacing the preview-local clipboard. New IDs are assigned, only the duplicate set is selected, and the operation creates one undo entry.

The annotation clipboard is scoped to the current preview window:

- `Ctrl+C` with selected objects copies those objects without changing the system clipboard.
- `Ctrl+C` without object selection copies the rendered image.
- `Ctrl+X` copies and deletes the selected set as one edit.
- `Ctrl+V` clones the internal set with new IDs and selects the clones.
- Repeated paste cascades from the previous pasted set by a small deterministic
  down-right offset. It does not change direction or clamp expanded hit-test
  bounds into the image.
- `Ctrl+D` uses the paste path without replacing clipboard contents.

The preview-local annotation clipboard and the system clipboard are separate mechanisms. `Ctrl+V` always pastes the preview-local annotation payload and never reads or changes the system clipboard. `Ctrl+Shift+V` explicitly reads the system clipboard asynchronously and never replaces or mutates the preview-local annotation clipboard.

System paste accepts one payload per invocation, preferring an image when the clipboard offers both image and text. Text preserves line breaks, rejects empty/whitespace-only or invalid Unicode payloads, and has a 256 KiB limit with no partial insertion. It uses the current Text color, size, font mode, and alignment, starts near the center of the visible viewport in image coordinates, is adjusted so its leading bounds remain inside the base image, selects only the new annotation, returns to Select, opens the Text properties HUD, and creates one undo entry.

System clipboard images become dedicated editable Image annotations rather than modifying the base bitmap. They own a decoded pixel copy, preserve aspect ratio, are centered in the visible viewport, scale down to the visible/base-image intersection with a screen-space margin, and never scale up during insertion. A singly selected Image keeps its selection box and adds four square corner handles. Corner resize uses `data.image.rect` as authoritative geometry, keeps the opposite corner fixed, locks aspect ratio, prevents flipping, enforces a usable screen-space minimum, and clamps the result to the base screenshot. Interactive resize changes only the destination rect; the renderer continues scaling the existing pixbuf and does not resample or replace it during drag. Images are limited to 16,384 px per side and 32 megapixels. They participate in preview render/export, Save, Copy, Done, selection, movement, multi-selection, duplicate, internal copy/cut/paste, delete, undo/redo, crop, cloning, and destruction. Empty or unsupported clipboard content produces the neutral transient message `Clipboard has no supported text or image.` without creating history, mutating the document or preview-local clipboard, or closing Preview. Failed, invalid, oversized, or changed payloads keep their specific actionable feedback.

Only one system read may be active. Key repeat is absorbed while a read is pending; closing the Preview cancels the operation silently. If the clipboard changes during a pending read, the payload is rejected instead of inserting stale or mixed content. When a GTK text editor has focus, GTK keeps normal text-level `Ctrl+A/C/X/V`, including its standard `Ctrl+V`; the Preview command router does not intercept those keys.

## Annotation Eraser

Annotation Eraser targets committed annotations only. It never erases pixels from the captured base image. Pixel Erase remains a contextual raster redaction action.

Core contract:

- Hit testing uses visible annotation geometry, not coarse bounds.
- Transparent interiors of unfilled shapes are not erase targets.
- A drag accumulates every touched annotation as a pending target.
- Pending targets render at 35% opacity.
- All pending targets commit as one undoable edit on release or tool exit.
- Empty gestures do not create history or mark the document modified.
- A click without movement still evaluates the initial eraser circle.
- Overlapping touched annotations are all marked, not only the topmost object.
- Save, Copy, Done, close, and switching tools commit pending targets first.
- `Esc` cancels an active gesture without mutation and exits to Select; with no gesture it exits to Select.

The tool has previous-tool toggle semantics:

- Pressing `E` from another tool enters Eraser and remembers that tool.
- Pressing `E` again restores the remembered tool.
- Confirming an erase gesture keeps Eraser active.
- Holding Space temporarily enters Hand/Pan and restores Eraser on release.
- Entering Pan during an active erase gesture commits pending targets first.

The drawn cursor and hit-testing use the same screen-space Size value. The HUD exposes Size from 8 to 48 screen pixels, defaults to 14, and steps by 2. Size persists as a last-used preference across launches.

During a drag, hit testing uses the swept capsule between pointer samples. The visual tail is separate from hit-testing, displays only the recent path, fades after release, and uses the theme foreground with alpha.

Pending erase annotations are not selectable and do not draw selection outlines or handles. Once pending during a gesture, they remain pending until commit or cancellation.

Naming and ownership:

- UI name: `Eraser`
- icon: `shaula-eraser-symbolic.svg`
- tool: `SHAULA_TOOL_ERASER`
- command: `SHAULA_PREVIEW_COMMAND_SET_TOOL_ERASER`
- operation: `SHAULA_OPERATION_ERASE_ANNOTATIONS`
- input/rendering: `src/preview/preview_canvas.c`
- pending/commit state: `src/preview/preview_state.c`
- geometry hit testing: `src/preview/preview_annotations.c`
- toolbar/shortcut entry points: `preview_toolbar.c`, `preview_commands.c`

Only primary-button click/drag erases. Right click has no Eraser-specific behavior.

## Arrow

`shaula-arrow-symbolic` is a one-shot creation tool.

After a valid arrow is drawn, preview selects it, opens the Arrow HUD, and returns to Select. Selecting the Arrow tool again prepares a new arrow instead of editing the selected object.

Select-mode hit testing is geometry-based:

- straight arrows hit near the visible shaft/head;
- curved arrows sample the curve path;
- the bend handle has explicit priority.

Selected arrows show a per-object bounding box. Start/end handles reshape the arrow, the control handle edits curvature, and dragging the shaft moves the object. Handles appear only for single selection. Arrow bounds include exact quadratic extrema and the visible arrowhead.

The Arrow HUD exposes stroke style: normal, dashed, or dotted. A real style change pushes undo before mutating the annotation and uses the same draw path for preview, copy, and export.

## Line

`shaula-line-symbolic` shares Arrow geometry, styling, hit testing, history, duplication, and clipboard behavior.

Line stores `data.arrow.has_head = FALSE` during draft and commit. `shaula_annotation_new_arrow` defaults to a headed arrow, so Line must explicitly clear the flag. Selected Lines use the same per-object bounding box and single-selection handles as Arrow, without arrowhead expansion. Their single-selection box uses visible line bounds rather than the broader geometry hit-test bounds, while multi-selection keeps the existing group-bound union contract.

## Text

`shaula-text-symbolic` uses the shared orange default and opens a floating HUD with:

- color;
- size;
- Normal/Sketch font mode;
- left/center/right alignment.

The font-mode control is a linked two-button segment with live `Ab` previews rendered in the resolved font families. Do not replace it with SVG icons.

A hidden `GtkTextView` owns keyboard input. The visible draft is rendered through the same Pango/Cairo annotation path as committed text, export, and copy.

Text invariants:

- `position` is the stable Pango layout origin.
- `text_line_metrics` owns draw origin, ink/logical union bounds, and line advance.
- Draw, selection bounds, draft bounds, and caret reuse those metrics.
- Text hit testing uses the exact selected bounds without additional click slop.
- Drafts show a canvas caret with a contrast halo.
- Caret position comes from the text buffer's UTF-8 insert byte index and Pango cursor position.
- `GtkTextBuffer::mark-set` redraws caret movement even when content is unchanged.
- Active drafts show text and caret, not an editing bounds rectangle.
- HUD changes during a draft update draft state without mutating a previously selected committed annotation.
- A singly selected committed Text annotation exposes four square corner handles.
- Corner resize changes `font_size` uniformly, recomputes bounds through Pango, and corrects `position` so the opposite visual corner remains fixed.
- Every drag update derives from gesture-start position and font size, avoiding cumulative Pango rounding drift.
- Resize preserves content, line breaks, alignment, color, and Normal/Sketch mode; it does not create wrapping or a fixed-width text box.
- Resizing an existing Text annotation does not change future Text creation defaults. Non-preset sizes leave all numeric HUD preset buttons inactive.
- Drag release must keep `SHAULA_OPERATION_TEXT` active while drafting.

Keyboard behavior:

- `Enter` inserts a newline.
- `Esc` or `Ctrl+Enter` finishes the draft.
- Clicking the canvas commits non-empty draft text without closing the preview.
- A canvas-only commit returns to Select with the new annotation selected.
- `Backspace` remains text-editor behavior while the hidden text view has focus.
- Selected-object deletion uses `Delete`.

Re-editing committed text:

- A second click (no drag) on a singly selected Text annotation reopens the string editor at that annotation.
- With the Text tool, clicking an existing Text annotation reopens it instead of creating a new draft.
- Re-edit loads the existing string, color, size, alignment, and font mode into the draft/HUD.
- The committed annotation is hidden while the draft is active so only one visible copy is drawn.
- Finishing a non-empty re-edit updates the same annotation (one undo entry when content or style changed).
- Finishing an empty re-edit deletes the annotation.
- HUD property changes during a draft restore focus to the hidden text view so typing can continue.

## Rectangle

`shaula-rectangle-symbolic` is a one-shot creation tool matching Arrow's post-create flow. A valid Rectangle becomes selected, opens its HUD, and returns to Select.

Defaults:

- color `#FD7603`;
- stroke width 3.5 px;
- dashed stroke;
- rounded corners;
- no fill.

HUD controls:

- color;
- stroke width;
- normal/dashed style;
- fill;
- rounded/square corners.

Fill uses the stroke color at low alpha.

Rectangle hit testing uses visible geometry:

- bounds are broad-phase only;
- unfilled interiors are not hit targets;
- only visible fills return `SHAULA_ANNOTATION_HIT_FILL`;
- handles and strokes rank above fills/text before z-order;
- transparent interiors pass clicks through to objects behind them.

Marquee selection follows the same visible-object contract. Arrows, Measure lines, Pen/Highlight paths, and unfilled Rectangles are selected only when the region intersects visible stroke geometry.

Selected Rectangles draw external selection chrome from actual Rectangle geometry with eight resize handles, then repaint the real stroke above the chrome.

Rotation, flipping, freeform Image distortion, Text wrapping/fixed-width boxes, and multi-selection resize remain out of scope.

## Pen

`shaula-pen-symbolic` opens a floating HUD for color, stroke width, and opacity. Defaults use the shared strong orange.

Pen hit testing uses path distance instead of bounds. A singly selected path shows one per-object bounding box with no endpoint handles or redraw pass. In multi-selection, the path contributes its bounds to the single group box instead. Bounds come from point min/max extents, so translating a path preserves box size.

After a valid Pen stroke commits, preview drops the selection so the next stroke starts uncluttered; the Pen HUD stays visible because it owns the tool-defaults editor. The committed path can still be re-selected later from Select or via marquee.

Future Pen styles belong in this HUD rather than the primary toolbar.

## Highlight

`shaula-highlight-symbolic` is separate from Pen and uses the highlighter icon, not the Spotlight icon.

Highlight is a wide, low-opacity freehand path with round caps. Its HUD exposes color, width, and opacity. A singly selected Highlight shows one per-object bounding box with no endpoint handles and no redraw pass that alters the visible color. In multi-selection, it contributes its bounds to the single group box instead. Freehand bounds come from point min/max extents, so translating a path preserves box size. It keeps its own semantics rather than inheriting future Pen brush styles.

After a valid Highlight stroke commits, preview drops the selection so the next stroke starts uncluttered; the Highlight HUD stays visible because it owns the tool-defaults editor. The committed path can still be re-selected later from Select or via marquee.

## Measure

`shaula-measure-symbolic` is implemented as the preview ruler/distance tool.

After a valid measurement commits, preview drops the selection so the next measurement starts uncluttered; the Measure HUD opens (or stays open) because it owns the tool-defaults editor for color and stroke width. The committed measurement can still be re-selected later from Select or via marquee.

## Tool Property Persistence

HUD-controlled tool options persist as per-tool, last-used preferences across
preview sessions rather than resetting on every launch.

The persistence contract covers every creation default exposed by a HUD:

- Arrow/Line: color, stroke width, and stroke style.
- Rectangle: color, stroke width, solid/dashed style, filled state, and
  rounded/square corners.
- Text: color, size, font mode, and alignment.
- Pen: color, stroke width, and opacity.
- Highlight: color, stroke width, and opacity.
- Measure: color and stroke width.
- Spotlight: border color, border width, and corner shape.
- Annotation Eraser: size.

Selecting an existing annotation loads that object's values into the inspector
without replacing the persisted creation defaults. A deliberate user change to
a HUD control updates both the selected object and that tool's future creation
default.

Defaults are stored in `$XDG_STATE_HOME/shaula/preview-tool-hud.ini`. Changes
are debounced for 500 ms and flushed when the preview closes. Concurrent preview
windows take a short file lock, reload the current INI, and merge only their
dirty tool sections so changes to different tools do not overwrite each other. Invalid or
missing values fall back independently to built-in defaults. Arrow and Line
share one default profile because they use the same HUD and stroke model.

## More Menu

`shaula-more-symbolic` exposes:

- Paste text/image (`Ctrl+Shift+V`), using `shaula-paste-symbolic` and the same icon-label row as the other actions, inserting clipboard text or an image near the visible canvas center;
- Save As;
- Fit to screen;
- Actual size;
- Reset annotations;
- Open preview directory.

Open preview directory resolves the directory containing the current preview path.

## Theme and Hover Behavior

Toolbar, overflow popover, and property HUD chrome follow GTK theme colors with explicit contrast reinforcement. Light themes such as Catppuccin Latte must use dark icon foregrounds even when the application prefers a dark theme.

Custom HUD icons read the active GTK foreground from the style context.

Toolbar and overflow icons use both GTK `:hover` and a `shaula-stable-hover` CSS class driven by pointer enter/leave. GTK may drop pseudo-hover state during keyboard input or capture grabs; the stable class keeps the visible hover capturable while the pointer remains over the button. Tooltips remain normal transient GTK popups.

Toolbar overflow must not use a permanent frame tick. Initial measurement uses a one-shot tick, and later updates come from `notify::width`. A continuous tick keeps `shaula-preview` active at idle.
