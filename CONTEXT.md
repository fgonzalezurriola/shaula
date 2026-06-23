# Context

Snapshot for prompt reuse. `./dev context` copies this file, the last 3 commits,
and the working diff.

## Current focus

- Quick/Area overlay confirmation now carries a typed action through the helper
  protocol and capture lifecycle. `Ctrl+C` confirms with direct copy as before;
  `Ctrl+S` confirms with direct save, forces a durable output in the configured
  save folder, and bypasses preview. The per-mode `copy_to_clipboard` setting is
  still resolved afterward, so direct save also copies only when that setting is
  enabled. Invalid, cancelled, or zero-sized helper envelopes cannot request
  these side effects.

- `shaula settings --json` is the built-in agent discovery entry point. It is
  read-only, does not launch GTK, does not capture pixels, and tells agents the
  recommended flow (`settings --json`, `doctor --json`,
  `capabilities list --json`, `explore --json [--brief]`, then capture) plus
  privacy constraints and stable JSON paths. Plain `shaula settings` still
  launches the GTK helper for humans.
- `shaula explore --json [--brief]` is the read-only desktop inventory command
  for agent visual loops. It emits the shared JSON envelope, compositor
  kind/label, focused output/workspace/window IDs, normalized Niri
  outputs/workspaces/windows, and `recommended_capture`. It never captures
  pixels or mutates desktop state; Niri inventory failures degrade to empty
  arrays with `explore_inventory_unavailable`. Window titles are exposed as
  visible desktop metadata and may contain sensitive user content.
- Generic Wayland support is now implemented through an xdg-desktop-portal
  Screenshot compatibility backend. Runtime selection prefers the test stub,
  forced portal, Niri direct, wlroots `grim` when `grim` is installed, and then
  portal for generic Wayland or wlroots without `grim`; generic Wayland without
  portal remains `ERR_UNSUPPORTED_COMPOSITOR`. The portal path uses the installed
  `shaula-portal-screenshot` helper (`SHAULA_PORTAL_SCREENSHOT_HELPER_BIN`
  override) with the same `--backend portal-screenshot --mode ... --output ...`
  contract as QA helpers. It reports degraded captures with
  `capture_backend_degraded`, portal-based area selection with
  `capture_selection_portal`, maps portal timeout to `ERR_IPC_TIMEOUT`, and maps
  portal cancellation to `ERR_SELECTION_CANCELLED`. Overlay selection remains
  limited to Niri/wlroots layer-shell; portal area capture bypasses overlay and
  `previous-area` is unsupported under the portal backend because the portal does
  not return reliable geometry. `preflight`, `capabilities list`, and `doctor`
  now expose portal availability/window-capability and backend selection.
- Wayland runtime architecture has a compact handoff context for future agents:
  `runtime/env.zig` owns borrowed environment parsing, `runtime/tool_lookup.zig`
  owns fixed `grim` candidates plus PATH-aware diagnostics lookup,
  `capture/backends/capture_backend_contract.zig` owns public backend labels, degraded
  warning tokens, and helper exit-code mapping, and `capture/warnings.zig` owns
  capture-specific warning tokens. `capabilities/runtime.zig` remains the runtime
  decision Module and exposes the leverage methods callers should use:
  `backendUsedLabel`, `degradedBackend`, `shouldBypassOverlaySelection`,
  `portalSelectionAvailable`, `previousAreaSupported`, and
  `selectPortalFallback`. Do not reintroduce string comparisons for
  `portal-screenshot`, `__stub__`, `portal_fallback`,
  `capture_backend_degraded`, `window_capture_degraded`, or
  `capture_selection_portal` outside those contract Modules.
- Config/preview architecture cleanup: `config/save_args.zig` owns the
  `shaula config save` setting-flag grammar and applies those flags to the
  config draft, while `config/command.zig` keeps command-level flags,
  orchestration, and JSON envelopes. `preview/preview_paths.{c,h}` owns the C
  helper-side temporary capture path contract used by preview state cleanup,
  copy notifications, Done/accept save promotion, and Save As default naming.
  Keep this aligned with `runtime/paths.zig`; do not reintroduce duplicate
  `/tmp/shaula/captures` or `XDG_RUNTIME_DIR/shaula/captures` checks inside
  preview action/state modules.
- Manual Wayland evidence planning lives in `docs/wayland-runtime-test-plan.md`.
  Preferred strategy is hybrid: use the real host for Niri, overlay, clipboard,
  notification, Noctalia, scale, and timing behavior; use VMs or spare machines
  for GNOME/KDE portal and extra wlroots coverage. Keep VM disks, temp files,
  captures, and Zig caches off `/` when root filesystem space is tight. On
  2026-05-31, direct QEMU/KVM evidence was captured under
  `/home/fgonz/dev/shaula-lab` for Fedora 44 Cloud and Ubuntu 26.04 LTS Cloud. Both
  guests verified official image checksums, copied the current working tree and
  local Zig 0.16.0, installed distro GTK/gtk4-layer-shell/wlroots portal
  packages, passed `zig build`, passed portal/unsupported-compositor contract
  smoke, passed `./dev check` inside Sway headless, and produced 1280x720 PNGs
  for `capture fullscreen` and `capture all-screens` with backend
  `grim-wlroots`. Running `./dev check` from plain SSH after installing `grim`
  fails the optional real-backend test because there is no active Wayland
  compositor; use the Sway headless wrapper for VM checks or run non-graphical
  package smoke before installing `grim`. Extra Ubuntu 26.04 headless desktop
  evidence: GNOME Shell 50 with `xdg-desktop-portal-gnome` exposed the
  Screenshot portal and Shaula reported backend `portal-screenshot`,
  `portal_available=true`, and `overlay_supported=false`; unattended fullscreen
  capture timed out because the portal needs an interactive prompt/window
  association. KWin/Plasma 6.6 virtual mode started, but
  `xdg-desktop-portal-kde` did not expose `org.freedesktop.portal.Screenshot`
  in that headless lane, so Shaula returned `ERR_UNSUPPORTED_COMPOSITOR` and
  the capture guard returned `ERR_CAPTURE_MODE_UNSUPPORTED` instead of
  pretending KDE portal capture was available. Detailed outputs live in
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/gnome-portal-test.out` and
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/kde-portal-test.out`.
- Preview selection uses `preview_annotation_editor.{c,h}` as its source of
  truth; `ShaulaPreviewState` stores selected IDs and no longer exposes a legacy
  `selected_annotation` pointer. Select supports click, Shift+click toggle,
  marquee intersection, Ctrl/Super+A, batch duplicate/delete, and moving a
  selected set as one history gesture. Single-selection handles and selection
  edges use fixed screen-space targets; multi-selection draws one group box.
  Hit testing follows visible geometry, so transparent interiors of unfilled
  shapes remain click-through and successful marquees clear the temporary
  region while unmatched drags remain available for contextual region actions.
- Preview annotation editing now has one deep Module at
  `preview/preview_annotation_editor.{c,h}`. It owns selection IDs, synchronization
  of persisted `annotation->selected` flags, single-selection derivation, HUD
  target derivation, selected bounds, annotation add/move/reset, clipboard
  copy/cut/paste/duplicate, batch delete, and the history/UI updates required by
  those mutations. `ShaulaPreviewState` stores the editor state but no longer
  exposes a legacy `selected_annotation` pointer or per-type annotation indexes
  in the properties HUD. Canvas, Commands, Actions, Toolbar, crop, and undo/redo
  restoration must cross the annotation editor interface instead of maintaining
  selection invariants in place. Rendering/export may continue to consume the
  synchronized `annotation->selected` flag as an internal hot-path contract.
- Preview pointer interpretation now crosses the deep
  `preview/preview_gesture.{c,h}` Module. It owns the `SHAULA_OPERATION_*`
  taxonomy, transient Select gesture state, screen/image normalization for hit
  routing, handle-versus-selection-edge-versus-geometry priority, hover cursor
  policy, multi-selection preservation, Move/Resize/Marquee transitions, drag
  updates, and selection history commit rules. GTK callbacks in Canvas only
  normalize button/modifier/coordinates, perform runtime-specific text, eraser,
  and live-measure work, apply returned cursor names, and request redraws.
  Selection gesture internals such as resize origins and pressed annotations
  must not be added back to `ShaulaPreviewState` as independent fields.
- Preview property behavior now crosses the deep
  `preview/preview_properties_hud.{c,h}` Module. Its Interface owns active panel
  and target derivation, selected-value versus tool-default resolution,
  validation/clamping, targeted history transactions, annotation and spotlight
  mutation, tool-default persistence, panel visibility, and GTK widget value
  synchronization. `preview_properties_panel.c` is only a widget-construction
  Adapter, `preview_tool_defaults.c` remains the persistence Implementation,
  Toolbar owns selection-action visibility only, and State no longer exposes
  property mutation functions. Property widgets must not read tool defaults or
  selected annotations directly when they are constructed or synchronized.
- Preview command routing now uses `preview/preview_commands.{c,h}` as the
  external command Interface for availability, shortcut lookup, tool-command
  mapping, and execution. `preview_actions.{c,h}` is an internal runtime
  Implementation for save/copy/window and sampled-color operations; only
  Commands may call it. `preview_action_callbacks.{c,h}` is the GTK Adapter and
  translates widget events into commands or property intents. Canvas, Toolbar,
  and property panels must not call runtime actions directly. This keeps
  availability and execution on the same path and prevents the former
  Commands-to-Actions-to-Commands dependency cycle from returning.
- Tool keyboard routing keeps Shaula's numeric toolbar slots while using
  unmodified mnemonic letters for equivalent Excalidraw tools: `V` Select, `R`
  Rectangle, `A` Arrow, `L` Line, `T` Text, `P`/`X` Pen, `H` Highlight, and `E`
  Annotation Eraser. Measure and Spotlight remain numeric only; Crop and
  contextual Blur have no letter binding; `Space` remains temporary Hand/Pan;
  and `F` remains Fit to screen. Shift-modified letters do not select tools.
  Commands owns the shortcut hints and numeric badges used by Toolbar tooltips
  and responsive More-menu rows.
- Documentation now treats Shaula as screenshot-only for v0.1.x: screen
  recording, OCR, scrolling capture, Share/upload, and Pin/window persistence
  are non-goals. Niri on CachyOS is the primary development and interactive UX
  target. Capture compatibility also covers wlroots compositors through `grim`
  and generic Wayland sessions through the Screenshot portal when available;
  X11 remains unsupported. Selection and helper geometry are logical output
  coordinates, while PNG dimensions, preview sampling, ruler measurements,
  redaction, and export operate on physical image pixels after output-scale
  normalization. Niri IPC/window semantics, Wayland screencopy migration,
  fractional scaling, and overlay teardown timing remain technical risks.
- `slurp` is no longer a runtime, installer, doctor, or AUR dependency. Region
  selection is owned by Shaula's GTK overlay; historical VM evidence may still
  list `slurp` among packages installed in those test guests.
- QA script curation: `scripts/qa/README.md` is now the source of truth for QA
  script status. The required baseline remains `./dev check` plus
  `git diff --check`; `./dev qa` is the curated non-intrusive contract lane
  (`run-all-tests.sh` -> `run-unit-tests.sh` -> preflight schema, failure
  matrix, exit-code mapping). Unsupported-compositor assertions use an explicit
  X11 token; Sway is valid wlroots coverage and must not be used as the negative
  fixture. Integration, E2E Niri, performance, release
  readiness, remaining benchmark, Noctalia, and intrusive UI wrappers are
  manual/legacy investigation tools and print an explicit warning before
  running. Unreferenced QA scripts for old repo preflight, UI smoke/contract
  checks, selection interaction shell checks, overlay cancel, and plugin
  overhead benchmarking were removed.
- Agent skill configuration is documented in `AGENTS.md` and `docs/agents/`.
  Skills should use GitHub Issues for `fgonzalezurriola/shaula` via `gh`,
  default canonical triage labels, and Shaula's single-context domain layout:
  root `CONTEXT.md` plus `docs/adr/` for durable architectural decisions.
- Scrolling capture is explicitly out of scope; keep the stale exploratory spec
  out of active roadmap decisions.

- Prompt handoff snapshot is now `CONTEXT.md`; `./dev context` copies it with
  the capture-mode note, last 3 commits, and working diff.
- Release, installer, AUR, icon packaging, and user integration contracts live
  in `docs/releasing.md`. Read that file before changing release workflows,
  install/setup behavior, package metadata, or managed Niri/Noctalia state.
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
  decoration behavior is unchanged. The interface is organized around user
  workflows (Capture behavior, After capture, Preview, Saving, Notifications, Niri
  integration, Advanced) with a compact capture-mode matrix for skip/copy/save
  combinations, a dedicated saving section, a readable Niri status card, and a
  fixed sticky footer for the primary Save and Cancel actions.
- Public docs live primarily in `README.md`. `docs/roadmap.md` defines v0.1.6
  as the UX rough-corners release: stabilize existing capture, preview/editor,
  feedback, setup, integration, and documentation flows before adding another
  large headline feature.
- The preview toolbar is the active UI surface.
- The goal is to keep the bar compact, useful, and honest about what is real.
- Undo/Redo now has a reusable preview history foundation for document edits.
- Preview commands now provide the shared dispatch path for toolbar callbacks,
  keyboard shortcuts, and future menus/configurable shortcuts.
- Capture/runtime foundation now has a shared process execution adapter and a
  single capture lifecycle module for capability guards, pre-capture guards,
  backend execution, previous-area persistence, and final JSON/`ERR_*`
  emission. Preview document/history and UI behavior were intentionally left
  unchanged.
- Frozen region capture now prepares the frozen source image in the capture
  lifecycle before launching the overlay helper. The helper receives that same
  image as its background, and Capture crops from the same frozen source instead
  of taking a second live desktop frame, preserving transient hover/tooltip
  state. Dev/release installs must ship `shaula-crop-image`; without that helper
  frozen quick/area captures fail before preview.
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
  with its last confirmed same-output area or a centered 60% default, shows a
  minimal aspect/Discard toolbar, confirms with `y`/Enter, cancels with
  Esc/Backspace/`q`/`n`, supports direct copy with Ctrl+C and direct save with
  Ctrl+S, persists area/aspect only on confirm, and opens the
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
  and `notify-send -i` fallback. Save/Ctrl+S is a neutral checkpoint and keeps
  the preview open; Save As/Ctrl+Shift+S also keeps the preview open. The Done
  headerbar action is the final action: it saves/promotes the current result if
  needed, copies the saved PNG to the clipboard when capture
  `copy_to_clipboard` requested copy-on-accept, emits the normal copy/save
  notification, and closes the preview. Enter
  maps to Done only when text/editable/crop/region/modal interaction does not
  own Enter.
- Copy-only screenshot notifications must not expose implicit runtime capture
  artifacts as clickable files. Captures without `--save`/`--output` still write
  an internal PNG under `$XDG_RUNTIME_DIR/shaula/captures` or
  `/tmp/shaula/captures` so preview/copy have real bytes, but those paths are
  not user-visible saved screenshots and must not become notification thumbnails
  or reveal targets.
- Preview `Enter` accept/save on an implicit runtime capture must promote the
  temporary PNG into the durable save folder (`save_folder`, default
  `~/Pictures/shaula`) before notifying. Never report `/run/user/.../captures`
  as a saved screenshot path; preview cleanup removes those staging files after
  a durable save, even when Done also copies the saved PNG to the clipboard.
- `skip_preview` and `copy_to_clipboard` are independent after-capture settings.
  When preview is skipped, copy runs immediately after capture. When preview is
  shown, the copy flag is carried into the GTK helper as
  `SHAULA_PREVIEW_COPY_ON_ACCEPT=1` so Done/Enter copies the accepted PNG after
  preview edits. First-run defaults expose `copy_to_clipboard=true` for quick,
  area, fullscreen, and all-screens; quick/area still show preview by default,
  while fullscreen/all-screens skip preview and save a durable copy to
  `save_folder` by default. Managed Ctrl+Shift+3/4 Niri keybinds also pass
  `--save`, so those shipped shortcuts always leave a screenshot file.
- The preview right headerbar cluster order is metadata, Done, then the normal
  window close X. Done is built during initial toolbar construction, uses
  `shaula-done-symbolic`, carries GTK `suggested-action`, and advertises either
  `Save (Enter)` or `Save and copy (Enter)` according to the copy-on-accept
  flag; Save remains neutral and is not a final/closing action.
- Preview toolbar is application content, not window decoration. It must not be
  installed with `gtk_window_set_titlebar()` because fullscreen compositors can
  hide titlebars/decorations. The helper builds a root content container with
  the compact toolbar above the canvas so the same toolbar remains visible in
  floating and fullscreen modes; empty toolbar spacer drag can begin a native
  window move in floating mode, while button drags remain normal UI input.
- Saved-screenshot notifications are actionable by default, with no setting.
  `src/notify.zig` owns the shared success path and publishes the freedesktop
  default action `default` labeled `Show in folder`; the listener still accepts
  `show-in-folder` and `reveal-file` for compatibility. Preview helpers receive
  the real path for the action listener but the visible notification body must
  stay generic (`Saved to screenshots folder.`), without filename or full path.
  `src/preview/preview_notify.zig` is the C-to-Zig bridge for immediate preview
  notification banners and reuses `notify/request.zig` for notify-send argv
  construction; `preview_actions.c` remains only the action caller/fallback
  policy owner.
  `SHAULA_BIN` and route canonical absolute saved paths through the same
  listener process. Activating the action tries
  `org.freedesktop.FileManager1.ShowItems` with a percent-encoded absolute
  `file://` URI, then falls back to `xdg-open` on the parent directory. Reveal
  failures are non-fatal and log the screenshot path, attempted method, and
  error from the listener under `$XDG_STATE_HOME/shaula/notify-actions.log`,
  `~/.local/state/shaula/notify-actions.log`, or `/tmp` as a last resort.
- Capture runtime terminology: internal capture planning now names
  `current-output` and `all-outputs` lanes separately from CLI compatibility
  tokens. Direct no-preview captures emit best-effort save/copy notifications
  from the Zig post-capture pipeline; preview keeps owning its own banners.
- Capture shortcuts are gated by a capture-only session lock. Rapid
  Ctrl+Shift+1/2/3/4 invocations return deterministic `ERR_CAPTURE_IN_PROGRESS`
  while selection/backend capture is active, but the lock is released before
  post-capture preview so existing previews do not block newer screenshots.
- The GTK preview helper runs as a non-unique `GtkApplication`, so multiple
  post-capture previews can stay open at the same time.
- Runtime and module ownership is documented in the Implementation Ownership
  Map in `spec/architecture.md`. Read it before adding capture, compositor,
  helper-process, overlay, post-capture, diagnostic, config, or preview seams.
- Implicit captures no longer save user-visible screenshots by default. Without
  `--save` or `--output`, the backend writes a temporary artifact under
  `$XDG_RUNTIME_DIR/shaula/captures` or `/tmp/shaula/captures` for preview/copy.
  `--save` still prefers `~/Pictures/shaula` (lowercase) and falls back to
  `~/shaula`; explicit `--output` still bypasses default resolution as a user
  save decision.
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
- Preview toolbar layout, overflow, startup-readiness, selection-chrome, and
  tool-placement contracts live in `docs/preview-ui-contract.md`.
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

## Overlay Capture

- Region capture now has an explicit mode foundation:
  - `live`: default normal Region path. The GTK layer-shell overlay draws only
    dimming/selection chrome and skips the frozen background so the desktop can
    keep updating while the user drags.
  - `frozen`: preserved intentional path for transient states. The full frozen
    screenshot is captured before the overlay helper is shown, displayed as the
    overlay background, and carried back as the immutable source for final crop,
    preview, copy, and save. The confirm path must not call `grim` again; if the
    frozen source is missing, capture fails with `ERR_CAPTURE_BACKEND_UNAVAILABLE`
    instead of falling back to a live capture.
- Frozen crop uses `shaula-crop-image` (`gdk-pixbuf`) to crop the stored PNG.
  The helper receives the overlay-local selection and the overlay surface size,
  then scales that rectangle into original image pixels so output scale,
  fit-to-screen stretching, and monitor pixel density stay tied to the actual
  frozen PNG dimensions. Manual check: hover a UI button until a tooltip is
  visible, run `./dev frozen`, select the tooltip, press Enter, and confirm the
  preview contains the same tooltip visible in the frozen overlay.
- Mode selection plumbing: CLI `--region-mode live|frozen`, convenience
  `--frozen-region`, env `SHAULA_REGION_CAPTURE_MODE`, config
  `[capture] region_capture_mode = "live"|"frozen"`, and `./dev frozen`.
  The built-in config and Settings helper defaults are `frozen`; `live` remains
  selectable for users who prefer a continuously updating selection view.
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
- Overlay Enter/Return is handled in key-capture phase by the overlay window:
  even when the aspect dropdown was just used or remains open, Enter closes the
  dropdown if needed and confirms the capture instead of reopening the menu.
- Overlay Ctrl+C confirms the current valid selection with helper
  `action:"copy"`; the Zig lifecycle turns that into immediate clipboard copy
  with preview disabled. Ctrl+S confirms with `action:"save"`, forces durable
  output, bypasses preview, and still follows the mode's configured copy setting.
  Q/N/Backspace remain cancel shortcuts.
- Area overlay hit-testing is handle-biased: the 8 corner and mid-point handles
  keep resize cursors, while edges themselves and the inner 24px rim keep move behavior,
  and dragging in the selection interior starts a new region. This makes it
  intuitive to move the selection by grabbing the edge, and keeps it
  cheap to recover from a too-large selection without hunting for empty screen
  space.
- Overlay selection stays output-local while the GTK layer-shell helper is
  interactive, then adds the selected monitor origin when emitting helper JSON
  for backend capture. Confirmed overlay restore state is stored per output
  name with output dimensions and diagnostic origin, but the rectangle itself is
  output-local. Startup restores only the current output entry and uses a
  preserve-size clamp (`w/h` fit first, then `x/y`) so a region saved on another
  monitor can never be flattened into the current surface. This keeps capture
  idempotent after Niri monitor hotplug/restart layouts where a single output
  may not sit at `0,0`, without adding retry sleeps to the hot path.
- Aspect-constrained capture remains available through
  `SHAULA_OVERLAY_ASPECT`; the removed button strip is no longer the interactive
  path for changing it during capture.

## Preview Tools

- Detailed action, selection, tool, HUD, clipboard, theme, and overflow
  contracts live in `docs/preview-tools.md`. Every HUD-controlled creation
  default now persists across preview sessions in
  `$XDG_STATE_HOME/shaula/preview-tool-hud.ini`; selected-object inspection does
  not overwrite defaults until the user changes a control. Writes are debounced,
  merged per dirty tool section under a short file lock, and flushed on close.
  Properties HUDs use a flat bordered surface without a drop-shadow glow; the
  Annotation Eraser value is shown beside its slider to keep the track centered.
  Pen, Highlight, Arrow, and Line use per-object bounding boxes for single
  selection. Highlight and Pen have no endpoint handles; Arrow and Line keep
  start/end/control handles only for single selection. Multi-selection replaces
  every per-object box with one group bounding box and no handles. Arrow/Line
  single-selection chrome uses visible geometry bounds, while broader annotation
  bounds remain unchanged for hit testing and the existing multi-select union.
  Freehand bounds are computed in C from explicit point min/max extents, while
  Arrow bounds include exact quadratic extrema and the visible arrowhead. Moving
  any of these annotations cannot change its box size.
  Repeated annotation paste uses a small deterministic down-right cascade and no longer
  changes direction or clamps expanded hit-test bounds into the image. `Ctrl+V`
  remains the preview-local annotation clipboard. `Ctrl+Shift+V` is an explicit,
  asynchronous system-clipboard command with image-over-text priority and no
  mutation of the internal clipboard. External text uses current Text defaults;
  external images become owned Image annotations with deep-cloned pixel payloads,
  viewport-centered downscaling, normal history/export behavior, and crop-aware
  payload remapping. The More-menu action is labeled `Paste from clipboard` and
  its tooltip explains center placement plus `Ctrl+Shift+V`. GTK text editors
  retain their native paste shortcuts.

## Visible Metadata

- Color swatch: implemented.
- Color hex label: implemented.
- Color swatch/hex are now live hover samples from the preview document under
  the cursor. The sampling path maps canvas coordinates through zoom/pan into
  image pixels, keeps the last valid sample when the pointer leaves the image,
  and samples the composited document pixel for the base image, stored
  annotations, and Spotlight effects while excluding GTK chrome, selection
  handles, temporary drafts, and floating HUDs.
- `Tab copy` is exposed beside the hex readout. `Tab` is routed through the
  shared preview shortcut map as
  `SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR`; it copies `#RRGGBB` only when a
  valid hover sample exists and lets focused editable widgets keep normal text
  navigation. The preview key controller uses GTK capture phase and a global
  `GtkShortcutController` fallback so `Tab` reaches the shared dispatcher
  before normal focus traversal outside editable widgets. `Tab` also refreshes
  the hover sample from the current GDK pointer position before dispatch, so
  copying does not depend on a prior motion event.
- The live hex label has a fixed pixel width and monospace glyphs so changing
  sampled colors does not shift the metadata group.
- Image dimensions and zoom are stacked in a single compact column using
  `PANGO_SCALE_SMALL` monospace: dimensions (`987×721`) on top, zoom (`100%`)
  below, both right-aligned. The compact fixed widths avoid blank space between
  the preceding swatch/hex/`Tab copy` group and the zoom column without
  changing order. Clicking the swatch applies the last valid sampled color to
  the selected annotation or the active tool's color default. This layout saves
  horizontal toolbar space compared to the previous inline readouts.

## Preview History

- `ShaulaPreviewDocument` lives in `preview_document.*` and owns
  output-affecting model state: current image buffer/path, annotations,
  Spotlight regions, preview-local annotation clipboard, next annotation id,
  modified/copied/saved flags, saved path, and history.
- Simple annotation add/remove/clear mutations should go through
  `preview_document.*`; selection and GTK invalidation remain in
  `ShaulaPreviewState`.
- `ShaulaHistoryStack` now belongs to `ShaulaPreviewDocument` and stores
  bounded document snapshots with undo/redo arrays and a default capacity of
  24. Snapshots hold a referenced immutable `GdkPixbuf`; pixel-mutating edits
  replace the image before mutation so annotation-only undo entries do not
  deep-copy full images.
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
- Routed shortcuts: Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+Shift+V, Ctrl+S,
  Ctrl+Shift+S, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, Ctrl+D, Delete, Ctrl/Super+A,
  `Tab`, `f`/`F`, and tool hotkeys `0`/`E` Eraser, `1`/`V` Select, `2`/`R`
  Rectangle, `3`/`A` Arrow, `4`/`L` Line, `5`/`T` Text, `6`/`P`/`X` Pen,
  `7`/`H` Highlight, `8` Measure, and `9` Spotlight. Space-held Hand/Pan
  is transient input state in
  `preview_canvas.c`, not a persistent shortcut command, so it can restore the
  previous selected tool safely.
- Toolbar/menu callbacks now dispatch through preview commands while existing
  low-level action helpers still own runtime work such as copy, save, discard,
  open folder, and tool cursor updates.

## Icon Assets

- Preview toolbar icons are rendered from Shaula's SVG assets instead of using
  GTK symbolic-mask recoloring, because these assets are stroke/outline icons.
  The loader replaces `currentColor` with a theme-appropriate foreground color
  before rasterizing, preserving stroke geometry in dark themes such as Nord.

## Gaps

- Share/upload backend, Pin/window persistence, OCR, scrolling capture, screen
  recording, deep redaction, AI removal, smart selection, and combine
  screenshots are explicitly out of scope.

## Planned: QuickShell Integration

A general QuickShell integration layer is planned to expand beyond the Noctalia-specific plugin. The design introduces `ShaulaService.qml` (a QuickShell Singleton + IpcHandler) that any QS-based shell can use, an IPC-based panel-hide handshake replacing file-token polling, and a standalone bar widget for non-Noctalia QS configs. Full plan: `docs/plan-quickshell-integration.md`. Phases: (0) extract shaula-core QML module, (1) refactor Noctalia plugin to use ShaulaService, (2) IPC-based panel-hide handshake + `shaula ipc` CLI subcommand, (3) bidirectional capture state feedback, (4) standalone widget distribution and setup.

## Post-v0.1.6: Landing page (`web/`)

- Web work is not part of the v0.1.6 UX-polish scope. The existing landing is
  preserved as-is; deployment and further content/visual polish resume after
  v0.1.6.
- `web/` is a static one-page landing built with `Astro 5 + Tailwind CSS 4`.
- Purpose: product page showing what Shaula is and how to install it.
- Structure: Hero (title + curl/AUR tabs with copy button + demo gif) -> Features
  (6 cards) -> Open source -> Support (Ko-fi).
- Commands: `pnpm install`, `pnpm dev` (localhost:4321), `pnpm build`,
  `pnpm preview`. All run from `web/`.
- Demo asset: `web/public/shaula-demo.gif` is copied from
  `docs/assets/shaula-demo.gif`. Replace with a better asset by dropping a new
  file into `web/public/` and updating `src/components/product-media.astro`.
- Visual direction: premium dark theme with a warm layered radial orange/amber glow behind the text and a subtle, dense white/gray grid (`48px`) that covers the hero section. Includes a three-line title typography where `with annotations.` is muted (`text-white/30`) and a description paragraph.
- Ko-fi link uses the real `https://ko-fi.com/fgonzalezurriola` from README.
- Deploy is not configured yet; the build outputs to `web/dist/`.
- The `web/` directory is independent from the Zig/GTK main codebase; `./dev
  check` and `git diff --check` are unaffected.
