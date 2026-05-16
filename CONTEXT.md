# Context

Snapshot for prompt reuse. `./dev context` copies this file, the last 3 commits,
and the working diff.

## Current focus

- v1.0.0 release prep: README install examples now point at `v1.0.0`, the
  roadmap copy no longer describes Shaula as early software, and the optional
  Noctalia widget manifest is aligned to `1.0.0`.
- Scrolling capture specification is now in `spec/scrolling-capture.md`. The strategy is hybrid: Phase 1 starts with manual-scroll + row-vote stitching (no virtual pointer dependency), Phase 2 adds auto-scroll via `zwlr_virtual_pointer_v1`. The row-vote stitching algorithm is ported from jbonney/scrollshot's proven approach (row-by-row voting with sampled SAD + seam finding in overlap middle 80%). Column-sample algorithm (from wayscrollshot, O(9*height)) is planned as a fast alternative. The `shaula-scroll-helper` Wayland client binary pattern matches Shaula's existing helper architecture (overlay, preview). Required Wayland protocols: `zwlr_layer_shell_v1`, `zwlr_screencopy_manager_v1`, `zwlr_virtual_pointer_manager_v1`. CLI: `shaula capture scrolling --scroll-mode auto|manual`.

- Prompt handoff snapshot is now `CONTEXT.md`; `./dev context` copies it with
  the capture-mode note, last 3 commits, and working diff.
- GitHub Releases are published by `.github/workflows/release.yml` on `v*` tag
  pushes. The release job builds from the tag with
  `zig build -Doptimize=ReleaseSafe -Dstrip`, packages `zig-out/bin` and
  `zig-out/share` as `shaula-linux-x86_64.tar.gz`, writes and verifies
  `SHA256SUMS`, and publishes/replaces release assets with `gh`. The publish job
  uses `contents: write`, does not run on PRs, does not use shared caches, and
  sets a tag-specific `run-name` for release runs.
- The public repository currently uses `master` as the default branch, so README
  installer snippets use `raw.githubusercontent.com/.../master/scripts/install.sh`.
- First installer foundation is now present in `scripts/install.sh` and
  `scripts/uninstall.sh`. It is user-local only, verifies GitHub release
  `SHA256SUMS`, supports latest stable, `--version`, positional `v*`, and
  `SHAULA_VERSION`, warns about missing runtime tools, installs
  desktop/icon/config/generated paths, never uses sudo, and preserves an
  existing `~/.config/shaula/config.toml`. Release installs are non-interactive;
  optional Noctalia installation is skipped unless explicitly confirmed with
  `--yes`, and local dev installs still prompt unless `--yes` is passed.
- `./dev dev-install [scripts/install.sh args...]` builds the current checkout,
  packages `zig-out` into a temporary local release archive with `SHA256SUMS`,
  and runs `scripts/install.sh` against `file://` URLs. Use
  `./dev dev-install --yes` for a non-interactive install of the exact local
  build. `./dev install` remains as a deprecated alias with a warning.
  The dev wrapper sets `SHAULA_INSTALL_CONTEXT=dev`, prints a local-dev banner,
  and makes install/Noctalia prompts explicitly say they are installing or
  reloading this local dev build rather than a GitHub release. When
  `~/.config/noctalia` exists, `dev-install` restarts Noctalia after a real
  install by trying `noctalia.service`, then `qs`, then `quickshell`; it skips
  restart for `--help`, `--uninstall`, and `--no-integrations`.
- `./dev dev-install --yes` does not opt into Niri keybind installation by
  itself; use `./dev dev-install --yes --niri-keybinds` or the Settings
  shortcut installer. Managed CTRL+Shift+1/2/3/4 Niri keybinds must spawn
  `shaula capture <mode> --json`; capture commands reject non-JSON invocations
  with `ERR_CLI_USAGE`, which makes compositor-spawned shortcuts appear to do
  nothing.
- Installer icon handling copies the packaged `share/icons/hicolor` tree into
  `~/.local/share/icons/hicolor`, not only the desktop app icon. Preview helper
  toolbar icons are runtime-loaded from `../share/icons` relative to the
  installed helper, so missing `scalable/actions/shaula-*-symbolic.svg` files
  show as GTK missing-icon glyphs in installed previews.
- Installer integration behavior is intentionally conservative: it detects Niri
  config candidates and generates
  `~/.config/shaula/generated/niri-shaula.kdl` without editing Niri config. The
  snippet includes the preview floating window-rule and recommended area,
  fullscreen, and all-screens capture binds using the installed absolute
  `shaula` path. It detects Noctalia and can optionally install the minimal
  `integrations/noctalia/shaula/` Bar Widget into
  `~/.config/noctalia/plugins/shaula/`, enabling `states.shaula.enabled` and
  adding `plugin:shaula` to the bar only after backing up and validating
  Noctalia JSON config. The user-facing Noctalia menu should expose capture,
  Settings, screenshots-folder, and bug-report actions; keep `shaula doctor`
  out of that menu because it is a diagnostic/development command.
- `shaula doctor` and `shaula doctor --json` now provide read-only diagnostics
  for installed paths, config/generated files, Wayland env, Niri candidates,
  Noctalia detection paths, Shaula Noctalia plugin install/enabled state,
  runtime tools, and actionable warnings.
- `shaula settings` now launches the native GTK `shaula-settings` helper.
  The settings window edits the existing public config contract only:
  live/frozen region capture mode, preview window mode/focus, close preview on
  save, floating size presets with custom width/height, and
  centered/top-left/top-right floating placement. Missing config opens with
  defaults and is created on first save;
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
  install/usage/dev-basics page with `docs/assets/shaula-preview.png` as the
  product preview image. `DEV.md` holds internal workflow and integration notes,
  and `docs/roadmap.md` tracks future features.
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
  truth (`Quick Capture`, `Capture Fullscreen`, `Capture All Screens`).
  Public capabilities now report `area`, `fullscreen`, `all_screens`, and
  `window`.
  `all-in-one` is no longer promoted in docs/scripts and remains only as a
  legacy CLI alias for the Capture overlay flow; `focused` remains a hidden
  compatibility alias for current-output fullscreen behavior.
- Capture Area restoration: `capture quick` is now the capture-on-release flow,
  while `capture area` is the adjustable overlay flow. The area overlay opens
  with its last confirmed area or a centered 60% default, shows a minimal
  aspect/Discard toolbar, confirms with `y`/Enter, cancels with
  Ctrl+C/Esc/Backspace/`n`, persists area/aspect only on confirm, and opens the
  normal post-capture preview. Quick and Area draft state are intentionally
  separate. Helper aspect extraction is parsed separately from `SelectionResult`
  ownership so custom ratios can persist without returning borrowed JSON memory;
  targeted contract tests cover quick mode routing, preview defaults, helper
  aspect override parsing, final aspect resolution, and aspect-store roundtrip.
  The GTK widget toolbar must compute placement against the monitor initial
  surface size until the drawing area has a real allocation; otherwise startup
  placement clamps to `(12,12)` before GTK measures the canvas.
- Preview Ctrl+S and Ctrl+Shift+C success banners now use `Screenshot captured`
  desktop notifications with Freedesktop `image-path` screenshot thumbnails
  and `notify-send -i` fallback. The `[preview.window] close_preview_on_save`
  setting defaults to true; when true, only successful Ctrl+S quick-save
  closes the preview after triggering the notification. Ctrl+Shift+S notifies
  after success but stays conservative and does not close the preview.
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
- Capture runtime resolution is upstream-owned: `capabilities/runtime.zig`
  selects the backend/runtime decision, `compositor/focused_output.zig`
  resolves focused-output names, and `capture/lifecycle.zig` passes those
  resolved values into `capture_backend.execute`. Backend execution and
  `capture_execution_plan.zig` no longer probe compositor/backend state.
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
- Implicit captures no longer save user-visible screenshots by default. Without
  `--save` or `--output`, the backend writes a temporary artifact under
  `$XDG_RUNTIME_DIR/shaula/captures` or `/tmp/shaula/captures` for preview/copy.
  `--save` still prefers `~/Pictures/shaula` (lowercase) and falls back to
  `~/shaula`; explicit `--output` still bypasses default resolution as a user
  save decision.
- v1.0.1 hardening: preview records the original implicit capture temp path and
  unlinks it on close when safe; copy notification thumbnail paths are preserved
  for notification daemons and cleaned by a conservative stale-temp sweep.
- `shaula directory screenshots [--open] [--json]` is the shared resolver for
  the durable screenshot folder. Noctalia's Open Screenshots Folder action
  calls this command instead of duplicating fallback path rules.
- Capture copy is also explicit by default: `--copy` is required for the Zig
  post-capture pipeline to touch the system clipboard. Area/all-in-one still
  open preview by default so the user can choose Copy, Save, Save As, or
  Discard.
- Noctalia and generated Niri capture actions use plain `shaula capture ...
  --json` commands by default so `[capture.after.*]` settings decide whether
  preview opens, clipboard copy runs, or the Shaula directory receives a file.
- Preview metadata readouts that update while interacting must reserve stable
  width. The color hex and zoom percentage labels both use fixed code-style
  widths so hover color sampling and zoom changes do not shift the toolbar.
- Preview toolbar actions are packed at the headerbar start, not as a centered
  title widget, so extra tools can use left-side space before overflowing into
  `...`. Overflow uses the measured gap between the toolbar start and the right
  metadata readout; using the full headerbar width can reveal secondary buttons
  too early and transiently overlap the color, dimensions, and zoom labels.
- Toolbar tool changes must not alter the preview window's natural titlebar
  width. Contextual Select actions live in a floating canvas HUD, like the
  Pen/Highlight property HUDs, so selecting tools or regions cannot ask GTK/Niri
  to resize the floating window.
- Pan and Crop are fixed navigation/utility tools after Copy, Save, Undo, and
  Redo. Numbered canvas tools are ordered Select `1`, Rectangle `2`, Arrow `3`,
  reserved Line `4`, Text `5`, Pen `6`, Highlight `7`, Measure `8`, and
  Spotlight `9`; only implemented numbered tools show subtle GTK keycap badges.
- Fit to screen, Actual size, and Reset annotations are responsive utility
  actions: they appear as icon buttons before `...` when there is room, and move
  back into the overflow menu when the headerbar is narrow. Save As remains a
  More-menu action.
- Spotlight regions are vector effect rectangles stored in image coordinates,
  matching the drag draft exactly. Do not route Spotlight creation through the
  crop/blur/erase pixel helper; that helper may round to integer pixels and is
  only for raster mutations. Default Spotlight border is the same strong orange
  used by arrow/text/measure defaults. Preview/export composition applies
  Spotlight effects to the captured image before drawing annotations, so text
  and vector marks added after Spotlights remain visible above the dimming.
- Direct Spotlight drag does not use `region_selection_rect`; that state belongs
  to Select-mode temporary region actions. Spotlight draft and commit both use
  `drag_start_image` plus `drag_current_image` so multiple or nested Spotlights
  cannot inherit stale Select-region geometry.
- Drag commits now apply one final update with the gesture's release `dx/dy`
  before ending the operation, so the committed geometry matches the exact
  release point even if GTK coalesces the last motion callback.
- Drag updates now derive pointer position from
  `gtk_gesture_drag_get_start_point()` plus `dx/dy` offsets, instead of
  reconstructing via image-space start and zoom/pan. This removes subtle
  coordinate drift across tools (including Spotlight) in edge cases.
- Preview C code uses `shaula_rect_clamped_c()` for interactive rectangle
  clamping. The `/diagnose` Spotlight logs showed `end-commit` geometry was
  correct while `persist` clipped against wrong bounds, so hot GTK C paths must
  not depend on the exported Zig `shaula_rect_clamped()` ABI for four-double
  structs plus scalar bounds.

## Capture Runtime Foundation

- `runtime/process_exec.zig` is the shared process execution adapter for
  capture/overlay runtime boundaries. Callers still own stdout/stderr limits,
  output cleanup, and deterministic failure mapping.
- `backends/capture_backend_failure.zig` centralizes backend `CaptureOutcome`
  failure construction so `capture_backend.execute` keeps one external seam
  while preserving deterministic `ERR_*` attributes at each failure site.
- `capture_backend.execute` receives a resolved runtime decision and optional
  focused output name. Tests should pass explicit decisions; do not reintroduce
  backend-local compositor/backend probes for test injection.
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
  parses the helper envelope. `HelperRunResult` exposes both `stdout` and `stderr` from the helper process.
- Debug overlay latency: `SHAULA_DEBUG_OVERLAY_LATENCY=1` measures CLI-to-overlay-UI-visible time. The parent records a launch timestamp before spawning the helper, passes the env var through, and the GTK overlay helper writes `SHAULA_OVERLAY_READY_TS=<epoch_ms>` to stderr after `gtk_window_present`. After the helper exits, the parent parses the ready timestamp and reports `[DEBUG-overlay-latency] launch_to_ui_visible=<ms>` to stderr. This is purely a debug/diagnostic feature gated behind an env var; it never ships active in production.
- `overlay/overlay.zig` is now a facade. `overlay/selection_session.zig` owns
  helper environment preparation, optional frozen background, deterministic
  dry-run/test payload handling, helper protocol mapping, and accepted-selection
  draft/UI persistence.
- The native GTK overlay used by `./dev capture` is now capture-on-release.
  In quick mode it no longer draws or requires the floating button strip:
  a valid create, move, or resize drag confirms the selection on release.
  Escape/Q still cancel and Enter still confirms as a keyboard fallback.
- Area mode (`capture area`) replaces the old Cairo-drawn toolbar with a real
  GTK widget toolbar using `GtkOverlay` + `GtkFixed` for floating positioning.
  The widget toolbar uses `@theme_bg_color`, `@theme_fg_color`, `@borders`, and
  `@accent_bg_color` CSS variables so it respects the active GTK theme. It
  contains an aspect ratio dropdown button with a popover list, a themed Capture
  button (accent color), and an Esc cancel button. The toolbar floats below or
  above the selection and repositions itself with the same logic as the old
  Cairo toolbar. Capture is only enabled when there is a valid selection.
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
- `shaula-save-symbolic` Save: implemented. `Ctrl+S` quick-saves to the current
  real save path, or promotes a temporary capture from
  `$XDG_RUNTIME_DIR/shaula/captures`/`/tmp/shaula/captures` into
  `~/Pictures/shaula/shaula-YYYY-MM-DD-HHMMSS.png`, falling back to
  `~/shaula` when the Pictures directory cannot be created or written.
  Quick Save updates preview save metadata but does not create undo history.
- Save As: implemented as a responsive utility/menu action and
  `Ctrl+Shift+S`. It opens a file chooser, writes a PNG to disk, and updates
  the current real save path so later `Ctrl+S` saves to the chosen file.
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
  is now an explicit middle-button drag gesture.
- `shaula-fit-to-screen-symbolic` Fit to screen uses the root-provided
  arrows-maximize SVG moved into the preview icon theme.
- `shaula-actual-size-symbolic` Actual size uses a simple currentColor `1:1`
  symbolic SVG and keeps `0` as the shortcut without showing a toolbar badge.
- `shaula-hand-symbolic` Hand/Pan: implemented as a view-only navigation tool.
  It is routed through `SHAULA_PREVIEW_COMMAND_SET_TOOL_HAND`, uses the existing
  pan operation, left-drag pans while active, and the cursor is `grab`/`grabbing`.
  Hand/Pan does not edit pixels, annotations, Spotlight regions, modified state,
  save state, or undo history. Holding Space temporarily switches to Hand/Pan
  unless an editable widget is focused; releasing Space restores the previous
  tool, and release during an active pan waits until the drag ends before
  restoring.
- `shaula-spotlight-symbolic` Spotlight: implemented as an independent primary
  toolbar tool outside Select-mode-only contextual actions. Activating it does
  not reuse the Select contextual toolbar; it lets the user drag a new area
  directly on the canvas, applies Spotlight on mouse release, and then opens
  the floating properties HUD. The existing Select-mode contextual Spotlight
  action still works for an already selected temporary region. Its icon is the
  Tabler filter-style glyph; do not apply this glyph to Highlight.
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
  new arrow instead of editing the selected one. Select-mode hit testing for
  Arrow is geometry-based: straight arrows hit near the visible shaft/head,
  curved arrows sample the curve path instead of accepting the whole bounds,
  and the selected bend handle keeps explicit priority. Selected arrows draw
  path-following selection chrome and repaint the arrow above it. Selected
  arrows expose resize handles at start/end plus the curve control handle;
  dragging the shaft moves the arrow, dragging handles reshapes it.
- `shaula-line-symbolic` Line uses the Arrow annotation geometry and Arrow HUD
  styling controls, but stores `data.arrow.has_head = FALSE` at draft and commit
  time. `shaula_annotation_new_arrow` defaults to `has_head = TRUE`; Line must
  explicitly clear it so draw/export/copy render a plain shaft without an
  arrowhead while still sharing stroke color, width, style, hit testing, history,
  duplicate, and clipboard behavior.
- Arrow stroke style: implemented in the Arrow properties HUD. Selecting an
  arrow exposes normal, dashed, and dotted toggles. Changing style mutates the
  selected arrow annotation, pushes undo before the document change, and keeps
  copy/export rendering on the annotation draw path.
- `shaula-text-symbolic` Text: implemented. Text uses the same orange default
  as arrows, opens a floating Text HUD for color, size, Normal/Sketch font
  mode, and left/center/right alignment, and stores font mode plus alignment on
  the annotation. The font-mode HUD is a compact linked two-button segmented
  control whose buttons render live `Ab` previews in the resolved Normal or
  Sketch families; do not replace it with SVG icons or repo-root icon files.
  Active text input keeps a
  hidden `GtkTextView` only as the keyboard buffer; the visible draft is drawn
  as a temporary text annotation through the same Pango/Cairo
  `shaula_annotation_draw` path used by committed annotations, export, and
  copy. Draft and commit therefore share image-coordinate anchoring, zoom/pan
  transform, font family/weight, size, color, opacity, multiline layout,
  alignment, and text bounds. Text `position` is the stable Pango layout origin;
  do not reinterpret it as changing ink top-left. `text_line_metrics` computes
  the layout draw origin, ink/logical union bounds, and line advance once, then
  committed rendering, selected bounds, draft editing bounds, and draft caret
  reuse those metrics. Active drafts always draw a canvas caret with a contrast
  halo, including non-empty text. The caret rect comes from the committed text
  layout path via `shaula_annotation_text_cursor_rect`, using the hidden
  buffer's UTF-8 insert byte index and `pango_layout_get_cursor_pos`;
  `GtkTextBuffer::mark-set` redraws cursor moves even when text content does
  not change. Active drafts also draw a faint dashed editing box from the same
  annotation bounds, distinct from selected-annotation chrome. Empty drafts use
  the caret rect, not the empty annotation bounds, as the editing-box base so
  the caret halo cannot visually overflow the initial box at low zoom. Text HUD
  changes during an active draft update the draft state and do not mutate a
  previously selected committed text annotation. Drag release must keep
  `SHAULA_OPERATION_TEXT` active; clearing it on `on_drag_end` breaks draft
  rendering and click-to-commit flow because the draft path is operation-gated.
  `Enter` commits non-empty text, `Shift+Enter` inserts a newline,
  `Escape` cancels text entry, and clicking back on the canvas commits
  non-empty text without closing. After a
  canvas-only text commit, Text returns to Select mode with the new annotation
  selected so it can be moved immediately.
- `shaula-measure-symbolic` Measure: implemented.
- `shaula-rectangle-symbolic` Rectangle: implemented as a one-shot creation
  tool matching Arrow's post-create flow. After a valid rectangle is drawn,
  preview selects the new rectangle, opens the Rectangle properties HUD, returns
  to Select mode, and leaves the annotation movable/duplicable through the
  normal selected-annotation path. Rectangle defaults are orange stroke
  (`#FD7603`), 3.5 px, dashed, rounded corners, and no fill. Its floating HUD
  exposes color, stroke width, grouped normal/dashed stroke toggles, a fill toggle,
  and grouped rounded/square corner toggles, separated by vertical dividers.
  This grouped 'linked' UI pattern is also used in Arrow, Text, and Spotlight HUDs
  for related option toggles. Fill uses the selected stroke color at low alpha in
  the draw/export path so filled rectangles mark an area without fully hiding
  screenshot content. Select-mode hit testing for Rectangle is geometry-based:
  bounding boxes are broad-phase only, stroke-only or visually transparent
  rectangles hit only near the four visible edges with human tolerance, and
  filled interiors are active only when the drawn fill alpha is at least `0.10`.
  The selection resolver ranks handles/strokes above visible fills and text
  bounds before applying z-order, so empty rectangle interiors pass through to
  objects behind them. Selected rectangles draw border-following selection chrome
  instead of a generic expanded bounds box and expose eight resize handles in
  Select mode. Rectangle selection chrome must stay subtle UI: draw a low-alpha
  external solid aura, redraw the real annotation stroke above it, and use
  compact screen-pixel-stable handles as the primary selected affordance. Do
  not draw a dashed white selection outline over rectangle borders, because it
  visually replaces dashed orange content.
- `shaula-highlight-symbolic` Highlight: implemented. Its icon is the
  highlighter glyph, not the Spotlight/filter glyph.
- `shaula-pen-symbolic` Pen: implemented.
- Pen secondary HUD: implemented as its own floating contextual HUD. Pen exposes
  color, stroke width, and opacity for defaults and selected Pen annotations.
  Pen defaults to the shared strong orange used by Arrow/Text/Rectangle/Measure.
  Select-mode hit testing for Pen and Highlight is path-distance based rather
  than bounding-box based, and selected freehand paths draw path-following
  selection chrome instead of a large rectangular box. Keep Arrow/Line and
  Pen/Highlight selection chrome low-alpha and solid; do not change their hit
  geometry while tuning the white outline. The real path is repainted above
  selection chrome so selecting a Pen/Highlight path must not visually turn its
  stroke white. Additional Pen styles are desired future work and should fit into
  this HUD rather than expanding the primary toolbar.
- Highlight highlighter: implemented as a separate Highlight button/HUD from
  Pen. Highlight is now a wide low-opacity freehand path with round caps, not a
  rectangle tool. Its HUD exposes only color, width, and opacity, and it avoids
  inheriting future Pen brush styles.
- `shaula-more-symbolic` More: implemented overflow menu.
- `shaula-discard-symbolic` Discard: implemented. Closes the preview and
  reports `discard`.

## Overflow Menu

- Save As: implemented.
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
  handles, temporary drafts, and floating HUDs. `shaula_color_to_hex` writes a
  null-terminated `#RRGGBB` into the fixed eight-byte C buffer; missing
  termination corrupts the GTK metadata label and can make the headerbar width
  jump while the invalid bytes disappear.
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
- Image dimensions and zoom are stacked in a single compact column using
  `PANGO_SCALE_SMALL` monospace: dimensions (`987×721`) on top, zoom (`100%`)
  below, both right-aligned. This layout saves horizontal toolbar space
  compared to the previous inline readouts.

## Preview History

- `ShaulaHistoryStack` lives in `preview_state.*` and stores bounded document
  snapshots with undo/redo arrays and a default capacity of 24. Snapshots hold a
  referenced immutable `GdkPixbuf`; pixel-mutating edits replace the image
  before mutation so annotation-only undo entries do not deep-copy full images.
- History tracks state that affects copied/saved output: current image buffer,
  annotations, spotlight regions, annotation ids, and modified status.
- History intentionally excludes view-only state: zoom, pan, fit mode, active
  tool, toolbar menu visibility, hover, and transient crop/text drafts.
- Switching from Select into a creation tool commits any pending properties HUD
  transaction, cancels transient operations, clears object/region selection, and
  then opens only that tool's default HUD when the tool has one. This prevents a
  selected annotation from retaining an incompatible properties panel. Switching
  to Hand/Pan is view-only and preserves selection/HUD state.
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
- Routed shortcuts: Ctrl+Shift+C, Ctrl+C, Ctrl+V, Ctrl+S, Ctrl+Shift+S,
  Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, Ctrl+D, Delete, Backspace, `Tab`, `f`/`F`,
  `0`, number tool hotkeys `1` Select, `2` Rectangle, `3` Arrow, reserved `4`
  Line with no current command, `5` Text, `6` Pen, `7` Highlight, `8` Measure,
  `9` Spotlight, and secondary letter tool hotkeys `V/A/R/T/S/P/H/M/C` plus `B`
  Blur and `E` Erase when their region command is available. Space-held Hand/Pan
  is transient input state in
  `preview_canvas.c`, not a persistent shortcut command, so it can restore the
  previous selected tool safely.
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
