# Context

Snapshot for prompt reuse. `./dev context` copies this file, the last 3 commits,
and the working diff.

## Current focus

- Prompt handoff snapshot is now `CONTEXT.md`; `./dev context` copies it with
  the capture-mode note, last 3 commits, and working diff.
- First installer foundation is now present in `scripts/install.sh` and
  `scripts/uninstall.sh`. It is user-local only, verifies GitHub release
  `SHA256SUMS`, detects `x86_64`/`aarch64`, warns about missing runtime tools,
  installs desktop/icon/config/generated paths, never uses sudo, and preserves
  an existing `~/.config/shaula/config.toml`.
- Installer integration behavior is intentionally conservative: it detects Niri
  config candidates and generates
  `~/.config/shaula/generated/niri-shaula.kdl` without editing Niri config. The
  snippet includes the preview floating window-rule and recommended area,
  fullscreen, and all-screens capture binds using the installed absolute
  `shaula` path. It detects Noctalia and can optionally install the minimal
  `integrations/noctalia/shaula/` Bar Widget into
  `~/.config/noctalia/plugins/shaula/`, enabling `states.shaula.enabled` and
  adding `plugin:shaula` to the bar only after backing up and validating
  Noctalia JSON config.
- `shaula doctor` and `shaula doctor --json` now provide read-only diagnostics
  for installed paths, config/generated files, Wayland env, Niri candidates,
  Noctalia detection paths, Shaula Noctalia plugin install/enabled state,
  runtime tools, and actionable warnings.
- `shaula settings` now launches the native GTK `shaula-settings` helper.
  The settings window edits the existing public config contract only:
  live/frozen region capture mode, preview window mode/focus, floating size
  presets with custom width/height, and centered/top-left/top-right floating
  placement. Missing config opens with defaults and is created on first save;
  invalid config shows `ERR_CONFIG_INVALID` with open/reset options; valid
  config comments/layout are preserved where practical by Zig
  `shaula config save --json`, not by the GTK helper. `Save` writes config and
  updates Shaula's managed Niri rule block, but does not reload or restart Niri
  or Noctalia. Noctalia's Settings menu item only launches `shaula settings`.
  The UI now uses system GTK theme colors, compact custom spacing/typography
  CSS, and native GTK4 `GtkDropDown` widgets instead of the deprecated
  `GtkComboBoxText` to avoid state and rendering glitches. Settings also uses
  a native GTK header bar/titlebar with window controls; Preview and Overlay
  decoration behavior is unchanged.
- Public docs have been split: `README.md` is now the shorter product-facing
  install/usage/dev-basics page, `DEV.md` holds internal workflow and
  integration notes, and `docs/roadmap.md` tracks future features.
- The preview toolbar is the active UI surface.
- The goal is to keep the bar compact, useful, and honest about what is real.
- `pin screenshot` is a roadmap item, not a current toolbar action.
- Undo/Redo now has a reusable preview history foundation for document edits.
- Preview commands now provide the shared dispatch path for toolbar callbacks,
  keyboard shortcuts, and future menus/configurable shortcuts.
- Capture/runtime foundation now has a shared process execution adapter and a
  single capture lifecycle module for capability guards, pre-capture guards,
  backend execution, previous-area persistence, and final JSON/`ERR_*`
  emission. Preview document/history and UI behavior were intentionally left
  unchanged.
- Capture naming update: `capture fullscreen` targets the current/focused
  monitor, `capture all-screens` preserves the old all-output fullscreen
  behavior, and `capture focused` remains as a CLI compatibility alias.
- Capture surface unification: Noctalia's menu is the current naming source of
  truth (`Capture Area`, `Capture Fullscreen`, `Capture All Screens`). Public
  capabilities now report `area`, `fullscreen`, `all_screens`, and `window`.
  `all-in-one` is no longer promoted in docs/scripts and remains only as a
  legacy CLI alias for the Capture overlay flow; `focused` remains a hidden
  compatibility alias for current-output fullscreen behavior.
- Capture runtime terminology: internal capture planning now names
  `current-output` and `all-outputs` lanes separately from CLI compatibility
  tokens. Direct no-preview captures emit best-effort save/copy notifications
  from the Zig post-capture pipeline; preview keeps owning its own banners.
- Architecture deepening pass: capture grammar now lives in
  `capture/command_grammar.zig`; capture invocation assembly lives in
  `capture/invocation.zig`; backend execution planning is typed in
  `backends/capture_execution_plan.zig`; backend failure construction lives in
  `backends/capture_backend_failure.zig`; overlay orchestration lives behind
  `overlay/selection_session.zig`; doctor discovery lives in
  `doctor/diagnostics.zig`; and post-capture work separates side effects,
  typed outcomes, and JSON rendering across `pipeline/post_capture.zig`,
  `pipeline/post_capture_types.zig`, and `pipeline/post_capture_json.zig`.
- Compositor seam foundation now exists in `compositor/runtime.zig` plus
  `compositor/focused_output.zig`. Runtime detection now distinguishes
  `niri`, generic Wayland compositors, and unsupported environments; current
  runtime scope still supports only `niri`, but unsupported Wayland sessions
  now report deterministic detected compositor labels for future expansion.
- Preflight/capabilities/output-resolution wiring now consumes the shared
  compositor seam. `preflight` and `capabilities list` error details include
  detected compositor labels, and focused-output resolution is adapter-backed
  (`niri msg -j focused-output` for Niri, no fabricated output for others).
- Runtime process seam was deepened: `runtime/process_exec.zig` now also owns
  stdin-pipe execution (`runWithPipeInput`), and preview/notify/clipboard
  command execution now routes through runtime process adapters.
- C-to-Zig migration pass: settings config contract logic now lives in
  `src/settings/settings_config.zig`; preview geometry lives in
  `src/preview/preview_geometry.zig`; preview image IO and preview clipboard
  runtime calls live in `src/preview/preview_image_io.zig` and
  `src/preview/preview_clipboard.zig`. The GTK helpers still call the existing
  C headers, but `build.zig` builds Zig objects and links them into the native
  helpers. The old C implementations and scratch GTK test file were removed.
  Remaining C should be treated as GTK UI/rendering surface unless a later pass
  first extracts the Preview document model from `ShaulaPreviewState`.
- Overlay runtime cleanup: legacy unused `OverlayRuntime` lifecycle scaffolding
  was removed from `overlay/runtime.zig`; production overlay execution keeps a
  single helper stdio seam via `runSelectionHelper`.
- CLI contract drift reduction: preview/history/errors/doctor/notify command
  families now reuse shared `cli/json.zig` envelope helpers for timestamps,
  escaping, and deterministic `ERR_*` error JSON.
- Default capture save path now prefers `~/Pictures/shaula` (lowercase) and
  falls back to `~/shaula` when `~/Pictures/shaula` cannot be created or is not
  writable; explicit `--output` still bypasses default resolution.

## Capture Runtime Foundation

- `runtime/process_exec.zig` is the shared process execution adapter for
  capture/overlay runtime boundaries. Callers still own stdout/stderr limits,
  output cleanup, and deterministic failure mapping.
- `backends/capture_backend_failure.zig` centralizes backend `CaptureOutcome`
  failure construction so `capture_backend.execute` keeps one external seam
  while preserving deterministic `ERR_*` attributes at each failure site.
- `capture/lifecycle.zig` owns the common capture lifecycle after mode-specific
  inputs are resolved: enforce capability support, enforce pre-capture guard,
  optionally settle after live overlay, execute the backend, persist previous
  area only on success, and emit the existing success/error JSON contract.
- `capture/command.zig` is now a strict dispatcher into the lifecycle module.
- `capture/invocation.zig` converts parsed capture flags and resolved geometry
  into the lifecycle invocation contract: public command token, backend mode,
  request mode, output/window/area fields, post-capture flags, previous-area
  persistence, and optional live-overlay settle mode.
- `capture/command_grammar.zig` owns capture flag membership and deterministic
  command-specific `ERR_CLI_USAGE` messages. `capture/command_flags.zig` now
  declares per-mode flag structs and delegates parsing to that grammar.
- `backends/capture_execution_plan.zig` now receives a typed operation
  (`area`, `current_output`, `all_outputs`, or `window`) instead of mode booleans
  so current-output fullscreen and all-screens capture cannot drift.
- `pipeline/post_capture.zig` gathers `PostCaptureOutcome` state for history,
  clipboard, and preview. `pipeline/post_capture_json.zig` owns the stable JSON
  envelope, duplicated top-level/result fields, and partial/degraded rules.

## Overlay Capture

- Region capture now has an explicit mode foundation:
  - `live`: default normal Region path. The GTK layer-shell overlay draws only
    dimming/selection chrome and skips the frozen background so the desktop can
    keep updating while the user drags.
  - `frozen`: preserved intentional path for transient states. The overlay
    prepares and displays the best-effort still background while selecting.
- Mode selection plumbing: CLI `--region-mode live|frozen`, convenience
  `--frozen-region`, env `SHAULA_REGION_CAPTURE_MODE`, config
  `[capture] region_capture_mode = "live"|"frozen"`, and `./dev frozen`.
- Live final capture happens only after the overlay helper exits. Shaula waits a
  short compositor settle barrier (`SHAULA_LIVE_REGION_SETTLE_MS`, default 50ms)
  before backend capture to avoid including its own layer surface; this remains
  the main interactive Wayland/Niri timing point to verify.
- The production overlay helper stdio run now goes through
  `overlay/runtime.zig`, which owns helper binary resolution, stdout/stderr
  limits, and process failure mapping before `overlay/helper_protocol.zig`
  parses the helper envelope.
- `overlay/overlay.zig` is now a facade. `overlay/selection_session.zig` owns
  helper environment preparation, optional frozen background, deterministic
  dry-run/test payload handling, helper protocol mapping, and accepted-selection
  draft/UI persistence.
- The native GTK overlay used by `./dev capture` is now capture-on-release.
  It no longer draws or requires the floating Capture/Esc/aspect button strip:
  a valid create, move, or resize drag confirms the selection on release.
  Escape/Q still cancel and Enter still confirms as a keyboard fallback.
- Overlay selection stays output-local while the GTK layer-shell helper is
  interactive, then adds the selected monitor origin when emitting helper JSON
  for backend capture. Persisted initial geometry subtracts that origin before
  clamping back into the local surface. This keeps capture idempotent after Niri
  monitor hotplug/restart layouts where a single output may not sit at `0,0`,
  without adding retry sleeps to the hot path.
- Aspect-constrained capture remains available through
  `SHAULA_OVERLAY_ASPECT`; the removed button strip is no longer the interactive
  path for changing it during capture.

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
- `shaula-select-symbolic` Select: implemented. Left-click selects
  annotations, left-drag on a selected annotation moves it, and left-click/drag
  from empty image space creates a temporary region selection. Clicking/dragging
  empty space outside the image clears selection without panning. Canvas panning
  is now an explicit middle-button drag gesture. The same icon is reused in the
  overflow menu for Fit to screen and Actual size.
- `shaula-spotlight-symbolic` Spotlight: implemented as an independent primary
  toolbar tool outside Select-mode-only contextual actions. Activating it does
  not reuse the Select contextual toolbar; it lets the user drag a new area
  directly on the canvas, applies Spotlight on mouse release, and then opens
  the floating properties HUD. The existing Select-mode contextual Spotlight
  action still works for an already selected temporary region.
- Selected annotation actions: implemented as a small contextual toolbar group
  that appears only while Select is active and an annotation is selected.
- Region selection actions: implemented as temporary Select-mode UI state.
  Region selections are not annotations, are not saved/exported, do not enter
  undo history by themselves, and expose contextual Crop, Blur, Erase, and
  Spotlight actions.
- Spotlight contextual properties: implemented as a floating top-right
  properties HUD built by `preview_properties_panel.*`, attached to the preview
  `GtkOverlay`, and driven by `active_properties_panel`. Applying Spotlight
  shows Back, color, border width, pointed-corner rectangle, and rounded-corner
  rectangle controls over the canvas without resizing the main toolbar. The HUD
  targets the just-created Spotlight entry through `active_spotlight_index`, so
  color, width, and corner style update that last applied Spotlight
  reactively while also becoming the defaults for the next Spotlight. Back uses
  a dedicated drawn chevron, hides the floating panel, clears the active
  Spotlight target, and returns to the normal toolbar state. While the HUD is
  open, the transient region-selection overlay is hidden so the stored
  Spotlight border remains visible. This panel/target state is UI/config state
  only and is excluded from undo/redo snapshots.
- The Spotlight properties HUD uses GTK symbolic theme colors for widget
  chrome: `@theme_bg_color`, `@theme_fg_color`, and `@borders`. Its custom
  Cairo-drawn back/shape icons read the widget foreground from the active GTK
  style context, so the HUD follows light/dark GTK themes instead of fixed
  dark-panel colors.
- Theme contrast policy: toolbar, overflow popover, properties HUDs, and
  custom SVG icon recoloring should follow GTK theme colors with explicit
  contrast reinforcement. Catppuccin Latte/light themes must prefer dark icon
  foregrounds even if `gtk-application-prefer-dark-theme` is set.
- `shaula-duplicate-symbolic` Duplicate selected: implemented. Available from
  the contextual group and `Ctrl+D`; clones the selected annotation, assigns a
  new id, offsets it by 12 px on both axes, selects the duplicate, and commits
  one undo entry.
- Annotation copy/paste v1: implemented as an in-memory clipboard scoped to
  the current preview window only. `Ctrl+C` copies the selected annotation
  without touching the system clipboard; `Ctrl+V` pastes that internal
  annotation through clone/new-id/offset/undo. The clipboard lives in
  `ShaulaPreviewState`, stays out of undo/redo snapshots, uses a list-shaped
  structure for future multi-selection, and currently stores one annotation.
  Paste prefers a 12 px up-right offset, falls back around the source if needed,
  clamps best-effort inside the current image, selects the pasted object, and
  repeats from the last pasted object when it remains selected. Pasting external
  `text/plain` as a text annotation is future work and should stay out of v1 so
  paste does not read the system clipboard yet.
- `shaula-crop-symbolic` Crop selected: implemented in the contextual group
  for selected rectangle annotations only. It dispatches through
  `SHAULA_PREVIEW_COMMAND_CROP_SELECTED`.
- `shaula-trash-symbolic` Delete selected: implemented. Available from the
  contextual group and Delete/Backspace when text entry is not active.
- `shaula-arrow-symbolic` Arrow: implemented as a one-shot creation tool.
  After a valid arrow is drawn, preview selects the new arrow, opens the Arrow
  properties HUD, and returns to Select mode. Moving and bend editing for
  existing arrows belong to Select mode; clicking Arrow again always prepares a
  new arrow instead of editing the selected one.
- Arrow stroke style: implemented in the Arrow properties HUD. Selecting an
  arrow exposes normal, dashed, and dotted toggles. Changing style mutates the
  selected arrow annotation, pushes undo before the document change, and keeps
  copy/export rendering on the annotation draw path.
- `shaula-text-symbolic` Text: implemented. Text uses the same orange default
  as arrows, opens a floating Text HUD for color, size, and left/center/right
  alignment, and stores alignment on the annotation. The edit preview is a
  styled multiline `GtkTextView`; committed rendering uses the same Pango/Cairo
  text path as export/copy so preview and final output share font weight,
  size, color, multiline layout, and alignment semantics. The editor scales its
  CSS font size by the current preview zoom and uses the visible image width as
  its editing line box so typed text does not reflow or clip differently at
  commit time. In Text editing and normal preview focus, `Enter` saves the
  current preview output to the current image path and closes the preview;
  `Shift+Enter` saves, copies that output to the image clipboard, and closes.
  While editing text, both shortcuts first commit non-empty text into the
  document. `Escape` cancels text entry, and clicking back on the canvas commits
  non-empty text without closing. After a canvas-only text commit, Text returns
  to Select mode with the new annotation selected so it can be moved
  immediately.
- `shaula-measure-symbolic` Measure: implemented.
- `shaula-rectangle-symbolic` Rectangle: implemented as a one-shot creation
  tool matching Arrow's post-create flow. After a valid rectangle is drawn,
  preview selects the new rectangle, opens the Rectangle properties HUD, returns
  to Select mode, and leaves the annotation movable/duplicable through the
  normal selected-annotation path. Rectangle defaults are orange stroke
  (`#FD7603`), 3.5 px, dashed, rounded corners, and no fill. Its floating HUD
  exposes color, stroke width, normal/dashed stroke, fill toggle, and
  rounded/square corners. Fill uses the selected stroke color at low alpha in
  the draw/export path so filled rectangles mark an area without fully hiding
  screenshot content.
- `shaula-highlight-symbolic` Highlight: implemented.
- `shaula-pen-symbolic` Pen: implemented.
- Pen secondary HUD: implemented as its own floating contextual HUD. Pen exposes
  color, stroke width, and opacity for defaults and selected Pen annotations.
  Additional Pen styles are desired future work and should fit into this HUD
  rather than expanding the primary toolbar.
- Highlight highlighter: implemented as a separate Highlight button/HUD from
  Pen. Highlight is now a wide low-opacity freehand path with round caps, not a
  rectangle tool. Its HUD exposes only color, width, and opacity, and it avoids
  inheriting future Pen brush styles.
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
- Color swatch/hex are now live hover samples from the preview document under
  the cursor. The sampling path maps canvas coordinates through zoom/pan into
  image pixels, keeps the last valid sample when the pointer leaves the image,
  and samples the composited document pixel for the base image, stored
  annotations, and Spotlight effects while excluding GTK chrome, selection
  handles, temporary drafts, and floating HUDs.
- `Tab to copy` is exposed beside the hex readout. `Tab` is routed through the
  shared preview shortcut map as
  `SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR`; it copies `#RRGGBB` only when a
  valid hover sample exists and lets focused editable widgets keep normal text
  navigation. The preview key controller uses GTK capture phase and a global
  `GtkShortcutController` fallback so `Tab` reaches the shared dispatcher
  before normal focus traversal outside editable widgets. `Tab` also refreshes
  the hover sample from the current GDK pointer position before dispatch, so
  copying does not depend on a prior motion event.
- `shaula_clipboard_copy_text` delegates to `/bin/sh -c` so the
  `printf … | wl-copy` pipeline is properly interpreted as a shell pipe.
  `g_spawn_command_line_sync` / `g_shell_parse_argv` do not interpret `|`.
- The live hex label has a fixed pixel width and monospace glyphs so changing
  sampled colors does not shift the metadata group.
- Image dimensions label: implemented.
- Zoom label: implemented.

## Preview History

- `ShaulaHistoryStack` lives in `preview_state.*` and stores bounded document
  snapshots with undo/redo arrays and a default capacity of 24 while snapshots
  include full image buffers.
- History tracks state that affects copied/saved output: current image buffer,
  annotations, spotlight regions, annotation ids, and modified status.
- History intentionally excludes view-only state: zoom, pan, fit mode, active
  tool, toolbar menu visibility, hover, and transient crop/text drafts.
- Switching from Select into a creation tool commits any pending properties HUD
  transaction, cancels transient operations, clears object/region selection, and
  then opens only that tool's default HUD when the tool has one. This prevents a
  selected annotation from retaining an incompatible properties panel.
- Existing wired operations: crop, annotation creation, selected annotation
  move, selected annotation duplicate/delete, properties HUD edits, and reset
  annotations. Annotation moves capture before-state on mouse down and commit
  one history entry on mouse up. Arrow/Pen/Highlight/Text/Spotlight property
  edits capture one before-state on the first real value change and commit that
  transaction when the HUD closes, the selection/tool changes, or undo/redo is
  requested, so slider drags do not create one undo entry per tick.
- Reset annotations cancels transient drafts, pushes exactly one pre-clear undo
  snapshot, clears annotations, and relies on the standard edit push to clear
  redo when a new annotation is created after undoing the reset.
- Crop pushes one undo snapshot only after the crop rect validates and a cropped
  pixbuf exists. Remaining annotations are translated to the new image origin;
  annotations outside the crop are removed. In Select mode, clicking Crop with
  a selected rectangle annotation crops to that rect and removes that selected
  guide annotation from the committed cropped document. Clicking Crop
  with a temporary region selection crops to that region and clears the region
  selection.
- Blur/Erase/Spotlight region actions are document edits dispatched through
  preview commands. Each validates/clamps the temporary region, prepares a new
  edit, then pushes exactly one undo snapshot before committing. Blur uses
  strong pixelation and Erase fills with the dominant quantized one-pixel border
  color around the region, averaging only the winning bucket so flat UI
  backgrounds beat antialiased text/shadow samples. If no dominant sample can be
  found, Erase falls back to the border average and then a neutral fallback; both
  Blur and Erase are destructive image pixel edits. Spotlight stores document
  effect entries in `spotlight_regions`
  instead of darkening pixels; each entry preserves rect, pointed/rounded
  corner style, border color, and border width. Preview and export render one
  overlay and clear all spotlight shapes from it, so multiple spotlights stay
  bright and darkness does not compound. Width `0` means no border. Changing
  Spotlight properties while the HUD is targeting the just-created entry edits
  that entry in place through the shared property transaction path; after the
  HUD is closed, property changes are defaults for future applications.
- Restoring history clears transient operations and rebuilds selection from
  cloned annotations to avoid stale pointers.

## Preview Commands

- `preview_commands.*` owns `ShaulaPreviewCommand`,
  `shaula_preview_execute_command`, command availability, and the static
  shortcut map.
- Routed shortcuts: Ctrl+Shift+C, Ctrl+C, Ctrl+V, Ctrl+S, Ctrl+Z,
  Ctrl+Shift+Z, Ctrl+Y, Ctrl+D, Delete, Backspace, `Tab`, `f`, and `0`.
- Toolbar/menu callbacks now dispatch through preview commands while existing
  low-level action helpers still own runtime work such as copy, save, discard,
  open folder, and tool cursor updates.

## Icon Assets Not Wired To The Bar

- `shaula-pin-symbolic` exists in the theme, but there is no current toolbar
  button or callback wired to it.
- Preview toolbar icons are rendered from Shaula's SVG assets instead of using
  GTK symbolic-mask recoloring, because these assets are stroke/outline icons.
  The loader replaces `currentColor` with a theme-appropriate foreground color
  before rasterizing, preserving stroke geometry in dark themes such as Nord.

## Gaps

- Share is hidden until a backend decision exists.
- Pinning is not exposed in the current preview toolbar.
- Redaction and deeper object editing are still future work.
