# Context

Compact engineering handoff for Shaula. `./dev context` copies this file, the
last three commits, and the working diff. Git remains the implementation history;
this document records only current behavior, ownership boundaries, hard-to-infer
contracts, active risks, and immediate work.

## Current focus

- v0.1.6 is a UX and reliability pass over existing capture, preview/editor,
  feedback, setup, integration, and documentation flows. Do not add another
  large feature before those paths are coherent.
- The preview toolbar and More menu are the active UI surfaces. Keep the
  headerbar compact, stable in natural width, and honest about available actions.
- Settings must expose the public config contract without inventing a second
  persistence format or bypassing `shaula config save --json`.
- `Ctrl+Shift+V` is the dedicated system-clipboard paste command. It is separate
  from preview-local annotation paste (`Ctrl+V`) and must preserve the async
  ownership rules in ADR 0001.
- Scrolling capture, recording, OCR, upload/share, Pin persistence, smart
  selection, AI removal, and screenshot combining remain out of scope.

## Current product behavior

### Capture

- Public capture modes are `quick`, `area`, `fullscreen`, `all-screens`, and
  `window` where runtime capabilities permit them. `focused` and `all-in-one`
  remain compatibility aliases and are not promoted in UI or documentation.
- Quick capture confirms a valid create/move/resize drag on release. Area capture
  keeps an adjustable selection, aspect control, Capture, and cancel actions.
- Quick/Area `Ctrl+C` confirms with immediate copy and no preview. `Ctrl+S`
  confirms with durable save, bypasses preview, and still follows the mode's
  configured copy setting.
- Region capture supports `live` and `frozen`. Frozen mode captures one immutable
  source before the overlay and crops that source after confirmation; it must not
  silently fall back to a second live capture.
- `fullscreen` means current/focused output. `all-screens` means all outputs.
- Capture commands use a capture-session lock. Overlapping hotkey invocations
  fail with `ERR_CAPTURE_IN_PROGRESS`; the lock is released before preview.
- Implicit captures write internal PNG artifacts under the runtime capture
  directory for preview/copy. Only `--save`, explicit `--output`, direct save,
  or preview acceptance creates a user-visible durable screenshot.
- Default durable resolution prefers `~/Pictures/shaula` and falls back to
  `~/shaula` only when the preferred directory cannot be created or written.
- `skip_preview`, `copy_to_clipboard`, and `save_to_folder` are independent
  per-mode settings. A skipped preview must still perform configured copy/save
  side effects.

### Preview

- Preview is a non-unique GTK application, so multiple preview windows may stay
  open concurrently.
- The toolbar is application content above the canvas, not a GTK titlebar. It
  must remain visible in floating and fullscreen modes.
- Save (`Ctrl+S`) is a checkpoint. It closes only after a successful save when
  `preview.window.close_preview_on_save` is enabled. Save As keeps preview open.
- Done/Enter is the final accept action when no text editor, crop, region, or
  modal interaction owns Enter. It promotes temporary input when needed, copies
  on accept when requested, notifies, and closes.
- Copy-only notifications must not expose runtime artifact paths. Saved
  notifications may reveal the durable file through the shared notification
  action listener, while visible notification text stays generic.
- Selection state is ID-based. Single and multi-selection, history restoration,
  annotation mutation, and selection flags must cross the annotation editor
  boundary rather than storing independent selected pointers.
- History stores output-affecting document snapshots with a default capacity of
  24. View state, tool state, hover, and transient drafts are excluded.
- Spotlight is a vector document effect rendered before annotations. It must not
  be routed through destructive blur/erase pixel helpers.
- Tool HUD defaults persist separately from selected-object inspection. Reading a
  selected object must not overwrite future creation defaults.
- `Ctrl+V` pastes the preview-local annotation clipboard. `Ctrl+Shift+V` reads
  the system clipboard asynchronously, prefers image over text, inserts one
  annotation near the visible canvas center, and never mutates the internal
  annotation clipboard.

### Settings and integration

- `shaula settings` launches the native GTK helper. `shaula settings --json` is
  the read-only agent discovery surface and does not launch GTK or capture.
- Missing config uses integrated defaults and is created on first save. Invalid
  config displays `ERR_CONFIG_INVALID` with open/reset paths.
- Settings saves through `shaula config save --json`; valid comments and layout
  are preserved where practical by the Zig config manager.
- Settings covers region mode, per-mode preview/copy/save behavior, save folder,
  preview window mode/focus/close-on-save/size/position, notification switches,
  and managed Niri integration controls.
- The integrated defaults are frozen region mode, `~/Pictures/shaula`, success
  and error notifications with thumbnails, floating 1100x720 preview, focused,
  normal column display, centered position, and close-on-save enabled.
- Existing custom preview dimensions, column display, and floating coordinates
  must survive an unrelated Settings save. The UI shows Custom for values that
  do not match its common presets and changes them only when a preset is chosen.
- Saving settings may update Shaula's managed Niri rule block. Shaula does not
  reload or restart Niri or Noctalia.
- Noctalia is optional. It invokes public CLI contracts and must never become a
  capture hot-path dependency.
- `shaula doctor --json`, `preflight --json`, `capabilities list --json`, and
  `explore --json [--brief]` are read-only runtime/discovery surfaces.

## Architecture and ownership

### CLI and capture lifecycle

- `capture/command_grammar.zig` owns capture flag membership and deterministic
  usage errors.
- `capture/command_flags.zig` owns per-mode parsed flag types.
- `capture/invocation.zig` maps flags and geometry into the lifecycle contract.
- `capture/lifecycle.zig` owns capability guards, pre-capture guards, optional
  live-overlay settling, backend execution, previous-area persistence, and final
  success/error emission.
- `capture/post_capture.zig` owns history, clipboard, preview, and notification
  side effects after artifact creation.
- `capture/post_capture_json.zig` owns the stable capture result envelope.
- `capture/backends/capture_execution_plan.zig` owns typed backend operations.
- `capture/backends/capture_backend_contract.zig` owns public backend labels,
  degraded warning tokens, and helper exit mapping. Do not duplicate those
  strings in callers.

### Runtime and helpers

- `capabilities/runtime.zig` owns backend selection. Call its typed decision
  methods rather than comparing backend strings.
- `runtime/env.zig` owns environment parsing; `runtime/tool_lookup.zig` owns
  fixed tool candidates and PATH-aware diagnostics.
- `runtime/process_exec.zig` owns shared process execution. Callers still own
  output limits, cleanup, and deterministic error mapping.
- `runtime/helper_resolution.zig` owns helper lookup order: explicit environment
  override, sibling binary, then PATH.
- Capture commands are not retried automatically because retries can create
  duplicate screenshots or repeated portal prompts.

### Overlay

- `overlay/overlay.zig` is the public facade.
- `overlay/selection_session.zig` owns helper environment, frozen source setup,
  helper mapping, and confirmed selection persistence.
- `overlay/runtime.zig` owns the helper stdio process boundary.
- `overlay/helper_protocol.zig` owns helper envelope parsing.
- `overlay/selection_draft_store.zig` owns persisted Quick/Area draft state.
- Overlay geometry is output-local while interactive and is converted to global
  capture coordinates only at the helper boundary.

### Configuration and settings

- `config/config.zig` is the typed public configuration model and default source.
- `config/loader.zig` owns strict TOML parsing. Unknown sections/keys and invalid
  values fail as `ERR_CONFIG_INVALID`.
- `config/save_args.zig` owns `shaula config save` setting flags.
- `config/manager.zig` owns canonical config output, comment-preserving patching,
  backups, and atomic replacement.
- `config/niri_rule.zig` owns generated preview window-rule semantics.
- `settings/settings_config.zig` owns the C-facing settings model and config-show
  JSON mapping. The GTK helper is an adapter; it must not write TOML directly.

### Preview

- `preview/preview_commands.{c,h}` is the external command interface for
  availability, shortcuts, tool mapping, and execution.
- `preview/preview_actions.{c,h}` owns save/copy/window runtime actions and is
  called only by Commands.
- `preview/preview_action_callbacks.{c,h}` is the GTK callback adapter.
- `preview/preview_annotation_editor.{c,h}` owns selection IDs, selection flag
  synchronization, annotation clipboard operations, batch mutations, and their
  history/UI updates.
- `preview/preview_gesture.{c,h}` owns pointer interpretation, operation state,
  hit priority, hover cursor policy, drag transitions, and selection history
  commits.
- `preview/preview_properties_hud.{c,h}` owns property targets, selected/default
  resolution, validation, mutation transactions, persistence requests, and
  widget synchronization.
- `preview/preview_system_clipboard.{c,h}` owns async desktop clipboard reads,
  cancellation, weak-window recovery, payload preference, and insertion calls.
- `ShaulaPreviewDocument` owns output-affecting model state and history.
- `preview/preview_paths.{c,h}` owns temporary capture-path recognition and must
  stay aligned with `runtime/paths.zig`.

## Important runtime contracts

- Public JSON commands use the shared envelope and deterministic `ERR_*`
  taxonomy. Do not replace typed outcomes with ad-hoc stderr parsing.
- Capability rejection happens before backend execution.
- Backend execution receives an already resolved runtime decision and must not
  probe compositor/backend state again.
- Helper boundaries use explicit argv/environment/stdout/stderr contracts; there
  is no resident daemon or private command socket.
- Frozen region confirmation must crop the pre-overlay source. Missing frozen
  source fails; no live fallback is allowed.
- Live region capture waits the configured settle barrier after overlay exit.
  `SHAULA_LIVE_REGION_SETTLE_MS` defaults to 50 ms.
- Temporary capture paths are implementation artifacts. They must not appear as
  saved paths, reveal targets, or notification thumbnails.
- Preview system-clipboard image annotations own deep-cloned pixel payloads.
  Undo, duplicate, crop, export, and later internal paste cannot depend on the
  clipboard provider remaining alive.
- System clipboard paste inserts at most one item, with image priority over text.
  Cancellation caused by clipboard change/window close is not a user-facing
  technical error.
- Preview rendering/export may consume synchronized annotation `selected` flags
  as a hot-path contract; mutations must still pass through the editor.
- Toolbar overflow uses measured available space and a stable requested action
  width. Contextual actions and HUD controls must not expand headerbar natural
  width.
- New code comments are English and reserved for contracts, boundaries, or
  non-obvious decisions.
- Required validation after every change:

  ```bash
  ./dev check
  git diff --check
  ```

- After UI changes also run:

  ```bash
  ./dev dev-install --yes
  ```

- Interactive preview/overlay changes require a reasonable targeted check and
  user validation through:

  ```bash
  ./dev capture
  ./dev all
  ```

## Active risks and known gaps

- Interactive Wayland behavior still depends on compositor timing, output scale,
  portal interaction, focus rules, and overlay teardown. Do not claim additional
  compatibility without direct evidence.
- Niri IPC/window semantics, monitor hotplug layouts, fractional scaling, and the
  live-overlay settle barrier remain sensitive runtime areas.
- Settings has multiple representations of each field (GTK model, config JSON,
  strict TOML, generated Niri rule, helper environment). Changes require an
  end-to-end consumer audit rather than local UI inspection; custom config values
  must not be collapsed into UI presets during round-trip saves.
- Preview system clipboard is asynchronous. Window destruction, provider change,
  unsupported payloads, and duplicate feedback must remain distinct outcomes.
- Image annotation history cost is proportional to pasted image payload size.
- Preview GTK/C orchestration remains large; respect module ownership to avoid
  recreating command/action/state dependency cycles.
- Manual VM and compositor evidence is maintained outside this handoff. Do not
  convert historical observations into stronger support claims.
- QuickShell general integration and the static landing page are planned work,
  not part of the current capture/preview reliability scope.

## Immediate next steps

1. Improve the More-menu system clipboard action with its own paste icon, a
   visible optional shortcut badge, and non-destructive unsupported-content
   feedback.
2. Run `./dev dev-install --yes`, `./dev check`, and `git diff --check` after the
   UI work. Perform a targeted preview check where the environment permits it.
3. Ask the user to validate interactive preview/capture behavior with
   `./dev capture` and `./dev all`.

## Relevant source documents

- `AGENTS.md` and `docs/agents/domain.md`: repository workflow and domain-doc
  policy.
- `spec/architecture.md`: public process, error, lifecycle, and ownership
  contracts.
- `spec/configuration.md`: public TOML schema, defaults, CLI, and Niri examples.
- `docs/adr/0001-preview-paste-architecture.md`: accepted async clipboard and
  image-ownership decision.
- `docs/preview-tools.md`: detailed preview actions, selection, clipboard, tools,
  HUD persistence, and More-menu behavior.
- `docs/preview-ui-contract.md`: headerbar packing, overflow, startup readiness,
  selection chrome, and placement contracts.
- `docs/releasing.md`: install/setup/release, icon packaging, and managed
  integration contracts.
- `docs/roadmap.md`: current release scope and non-goals.
- `scripts/qa/README.md`: maintained and manual QA lanes.
- `docs/wayland-runtime-test-plan.md`: manual runtime evidence plan. Preserve its
  claims until new direct testing is available.
