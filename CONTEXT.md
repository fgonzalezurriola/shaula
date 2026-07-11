# Context

Compact engineering handoff for Shaula. `./dev context` copies this file, the
last three commits, and the working diff. Git remains the implementation history;
this document records only current behavior, ownership boundaries, hard-to-infer
contracts, active risks, and immediate work.

## Current focus

- v0.1.5 is the latest released baseline, published on 2026-06-24 after manual
  Wayland/Niri validation. Release notes live in `docs/release-v0.1.5.md`.
- v0.1.6 is the image composition and expandable canvas release. It has no
  fixed date and should ship only when the new document model, history, export,
  memory limits, and human workflow are coherent. See `docs/plan-v0.1.6.md`.
- The `port` branch is the accepted Zig-to-C migration line. Its implementation
  order, compatibility gates, Meson target, and normative ownership rules are
  defined in `spec/zig-to-c-port.md` and ADR 0002. Port commits must preserve
  product behavior and remain separate from unrelated feature work.
- Phase 0/1 implementation has started. `docs/port/baseline.md` records the
  executable/toolchain/test baseline, `docs/port/migration-matrix.md` maps every
  Zig module, and Meson builds isolated C port tests without owning production
  installation yet. The C port workflow runs GCC and Clang with ASan/UBSan and
  includes fixture-driven runtime environment, Settings process, and Noctalia
  restart tests.
- Preview geometry is the first production caller cutover: `shaula-preview` and
  the Preview document test compile `preview_geometry.c`. Preview image I/O is
  the second: `shaula-preview` compiles `preview_image_io.c` directly for byte
  copies, PNG suffix handling, and containing-folder launch. Preview clipboard
  is the third: PNG copy uses the public Shaula CLI and text copy writes exact
  bytes to `wl-copy` through an explicit stdin pipe with no shell. Preview
  notification is the fourth: C owns exact `notify-send` argv, hint-to-icon
  fallback, timeout flags, URI escaping, and silent best-effort failures. The
  production Preview helper no longer links an aggregate Zig bridge. Settings
  configuration is the fifth Phase 2 cutover: `shaula-settings` compiles
  `settings_config.c` directly for defaults, preset mapping, path resolution,
  and config-show JSON mapping. `settings_process.c` owns exact save argv and
  synchronous subprocess behavior. Phase 2 strict cleanup is complete: the five
  obsolete implementation sources, the unlinked Preview bridge marker, and
  `runtime/c_compat.zig` have been deleted after repository-wide reference
  checks. All seven Phase 3 runtime primitives now have production C cutovers:
  `runtime/env.{c,h}` owns environment parsing, `runtime/paths.{c,h}` owns
  runtime-state path resolution, temporary-capture classification, and parent
  creation, `runtime/tool_lookup.{c,h}` owns fixed grim candidates, PATH
  splitting, and filesystem existence checks, `runtime/helper_resolution.{c,h}`
  owns override/sibling/bare-name helper precedence,
  `runtime/previous_area_store.{c,h}` owns previous-area serialization, parsing,
  file I/O, and backend gating, `runtime/capture_session_lock.{c,h}` owns
  exclusive acquisition, PID contents, stale-owner replacement, and best-effort
  release, and `runtime/process_exec.{c,h}` owns direct argv execution, exact
  parent-PATH lookup, bounded dual-stream capture, replacement environments,
  binary stdin, termination mapping, and child cleanup. Every maintained Zig
  caller now invokes those C ABIs directly through caller-local ownership/status
  conversion; repository-wide facade imports are zero, the obsolete facade
  bodies/tests have been removed, and the seven former facade paths have now
  been physically deleted. Phase 4 has four completed pure-model/shared-policy
  cutovers.
  `core/capture_mode.{c,h}` owns exact CLI/region tokens, runtime and backend
  lane mapping, and interactive-selection policy. Capture, configuration, and
  overlay callers include the C header directly and keep only caller-local
  span/status adaptation; its obsolete Zig path has also been physically
  deleted. `preview/preview_result.{c,h}` owns the length-aware final helper JSON
  parser and exact action tokens. `preview/service.zig` includes that header
  directly, maps fixed-width statuses/actions locally, and copies an optional
  GLib-owned saved path into its existing Zig allocator before clearing the C
  result. The obsolete `preview_result.zig` path has been physically deleted.
  `errors/taxonomy.{c,h}` now owns the
  canonical 28-entry public error table, class and recovery-action tokens, exact
  lookup, retry budgets, and exit-code fallback. Every maintained command caller
  includes the C header directly; `errors/command.zig` retains only command-level
  JSON emission. Static records and tokens are borrowed process-lifetime
  literals, enums cross the ABI as asserted 32-bit values, and unknown or invalid
  spans collapse to `ERR_UNKNOWN_UNMAPPED` with exit code 99. The currently
  emitted but unlisted `ERR_PREVIEW_RESULT_INVALID` intentionally preserves that
  fallback behavior. The obsolete taxonomy/policy Zig paths
  (`errors/taxonomy.zig`, `recovery/policy.zig`, `recovery/policy_test.zig`) have
  been physically deleted.
  `cli/json.{c,h}` now owns the public contract-version literal, exact byte-level
  JSON string escaping, warning-array serialization, UTC timestamp formatting,
  and complete basic error envelopes. Inputs are borrowed explicit-length byte
  spans; invalid UTF-8 and non-ASCII bytes are preserved, embedded NUL and other
  controls are escaped, and returned buffers are GLib-owned until cleared.
  Maintained command callers include the C header directly and retain only
  caller-local ABI/allocator adaptation plus their typed result/detail fields.
  `ipc/protocol.zig` now owns only `ipc_version`. The obsolete `cli/json.zig`
  path has been physically deleted.
- The preview toolbar and More menu are the active UI surfaces. Keep the
  headerbar compact, stable in natural width, and honest about available actions.
- Settings must expose the public config contract without inventing a second
  persistence format or bypassing `shaula config save --json`.
- `Ctrl+Shift+V` is the dedicated system-clipboard paste command. It is separate
  from preview-local annotation paste (`Ctrl+V`) and must preserve the async
  ownership rules in ADR 0001.
- Image composition and bounded canvas expansion are active v0.1.6 scope,
  documented in `docs/plan-image-composition.md` and
  `docs/preview-expandable-canvas-design.md`.
- Related bugs, UX polish, ownership refactors, and integration/documentation
  fixes discovered during implementation may enter v0.1.6. Unrelated large
  features still require separate scope.
- Scrolling capture, recording, OCR, upload/share, Pin persistence, smart
  selection, AI removal, infinite canvas, and global clipboard history remain
  out of scope.

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
- A singly selected Image or committed Text annotation exposes four corner
  resize handles. Image resize mutates only `data.image.rect`, preserves aspect
  ratio, prevents flipping, and clamps to the base screenshot. Text resize
  derives each update from gesture-start position/font size, scales the font
  uniformly, and reanchors the opposite visual corner after Pango bounds
  recomputation. Resize remains single-selection only, creates at most one undo
  entry per drag, and never updates future Text creation defaults.
- Pen, Highlight, and Measure are continuous-drawing tools: after a valid stroke
  commits, preview drops the selection so the next stroke starts uncluttered,
  and the tool-defaults HUD stays visible because it owns the editor for color,
  width, and opacity. The committed stroke can still be re-selected later from
  Select or via marquee.
- `Ctrl+V` pastes the preview-local annotation clipboard. `Ctrl+Shift+V` reads
  the system clipboard asynchronously, prefers image over text, inserts one
  annotation near the visible canvas center, and never mutates the internal
  annotation clipboard. Its More-menu row uses `shaula-paste-symbolic`, the
  compact label `Paste text/image`, and the same icon-label layout as the other
  actions; the shortcut remains in the tooltip.
- Clipboard content with neither supported text nor image shows the neutral
  transient message `Clipboard has no supported text or image.` It creates no
  history entry, does not mutate the document or internal clipboard, and does
  not close Preview.

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
  capture hot-path dependency. Development install/reload commands import only
  allowlisted missing graphical-session variables from the user systemd
  environment. Direct restarts wait for old instances to disappear, launch
  without duplicate permission, and require a newly observable running
  `noctalia-shell`; systemd restarts also require instance or active-service
  readiness before reporting success.
- Release and development installs refresh the user hicolor icon cache and
  desktop application database after desktop/icon changes when the host cache
  utilities are available. Cache refresh failures warn but do not invalidate an
  otherwise successful install.
- The desktop launcher uses `Icon=shaula`. App icon packaging includes fixed
  PNG sizes and a raster-preserving `scalable/apps/shaula.svg` generated from
  `src/preview/icons/source/shaula-source.png`; Noctalia/Quickshell can
  otherwise resolve the icon name to a missing scalable app icon.
- Release archives and `shaula-bin` must install and verify all helper binaries,
  including `shaula-portal-screenshot`.
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

### Core, runtime, and helpers

- `errors/taxonomy.{c,h}` owns the canonical public `ERR_*` inventory and
  ordering, exact messages, retryability, failure classes, recovery actions,
  retry budgets, and exit codes. Failure-class and recovery-action values are
  fixed 32-bit ABI integers. Records and text spans borrow immutable
  process-lifetime literals; lookup is byte-exact and rejects case changes,
  whitespace, prefixes, suffixes, non-ASCII substitutions, embedded NUL bytes,
  and invalid spans. Unknown or unmapped inputs return the final
  `ERR_UNKNOWN_UNMAPPED` record, exit code 99, and retry budget 0. Maintained Zig
  callers include the header directly and perform only immediate slice/span
  conversion. `errors/command.zig` owns only command parsing and enumeration of
  the C table; shared timestamp, contract-version, escaping, and basic-error
  envelope policy comes from `cli/json.h`.
- `cli/json.{c,h}` owns shared public JSON policy. `shaula_json_contract_version`
  returns borrowed immutable process-lifetime bytes. String, warning-array,
  timestamp, nullable-string, and error-envelope builders return GLib-owned length-bearing buffers
  with trailing-NUL storage; callers must clear them with
  `shaula_json_owned_bytes_clear`. String inputs are arbitrary bytes, not
  NUL-terminated text: quote and backslash use short escapes, named controls use
  `\\b`, `\\f`, `\\n`, `\\r`, and `\\t`, other bytes `0x00..0x1f` use lowercase
  `\\u00xx`, slash is unchanged, and every byte at or above `0x20` other than
  quote/backslash is copied unchanged, including valid or invalid UTF-8. Absent
  nullable strings encode as `null`; present empty spans encode as `""`. Basic
  errors preserve canonical field order and exactly one final newline; raw
  details fragments remain caller-owned and unvalidated for compatibility.
- `core/capture_mode.{c,h}` owns capture-mode enum tables, exact CLI and region
  parsing, canonical tokens, runtime/backend lane mapping, and interactive
  selection requirements. Header assertions and the C unit test pin every enum
  ordinal. Maintained capture, configuration, and overlay Zig callers include
  the C header directly, use the fixed-width C ABI values, and perform only
  immediate caller-local span/status conversion.
- `capabilities/runtime.zig` owns backend selection. Call its typed decision
  methods rather than comparing backend strings.
- `runtime/env.{c,h}` owns environment trimming, boolean parsing, exact bounded
  unsigned parsing, and desktop-token extraction. It returns borrowed spans and
  allocates nothing. Zig callers perform only caller-supplied environment lookup
  and immediate ABI conversion at their owning module.
- `runtime/paths.{c,h}` owns override/runtime/tmp fallback ordering, byte-exact
  joins, checked GLib-owned output allocation, runtime-capture markers, and
  parent creation. It performs no normalization or canonicalization. Zig owners
  copy GLib-owned results into their existing allocator only where their public
  API still requires Zig-owned bytes.
- `runtime/tool_lookup.{c,h}` owns fixed grim candidates, first-existing
  absolute lookup, byte-exact PATH splitting/joining, and existence-only
  diagnostics. It intentionally does not require executable permission and
  skips empty PATH components.
- `runtime/helper_resolution.{c,h}` owns helper lookup order: nonempty
  ASCII-trimmed environment override, existing sibling binary, then an owned
  bare binary name. The bare name deliberately defers PATH lookup to later
  process spawning. Sibling checks are existence-only.
- `runtime/previous_area_store.{c,h}` owns the exact
  `x|y|width|height\n` format, whole-file ASCII trimming, Zig-compatible numeric
  parsing, parent creation, synchronous file I/O, fail-closed loads, and exact
  portal-backend exclusion. Capture lifecycle resolves the caller-supplied path
  and converts only geometry/status values.
- `runtime/capture_session_lock.{c,h}` owns recursive parent creation, exclusive
  lock-file creation, exact PID serialization, bounded stale-owner detection,
  one-shot stale replacement, and best-effort release. Capture lifecycle retains
  only the resolved path and active flag needed to preserve release-before-
  Preview scope and idempotent cleanup.
- `runtime/process_exec.{c,h}` owns shared process execution: literal argv,
  Zig-compatible parent-PATH search, direct `execve`, replacement environments,
  concurrent bounded stdout/stderr drainage, binary stdin, SIGPIPE containment,
  termination classification, and child cleanup. Callers still own explicit
  output limits, returned-buffer cleanup, and deterministic command-specific
  error mapping; no subprocess policy remains in a Zig facade.
- Capture commands are not retried automatically because retries can create
  duplicate screenshots or repeated portal prompts.
- `zig build -Dstrip` strips the main executable and every native helper. Helper
  link commands use the minimal pkg-config module set and retain `-lm` only for
  Preview, where it is required.
- The overlay helper is implemented solely by
  `overlay/native_gtk_overlay.c`; the obsolete Zig wrapper entry points were
  removed.

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
- `settings/settings_config.{c,h}` owns the C-facing settings model, integrated
  defaults, config path resolution, preset mapping, ABI layout assertions, and
  permissive config-show JSON field extraction. Returned strings are GLib-owned
  and cleared through the public config cleanup function.
- `settings/settings_process.{c,h}` owns exact `config save --json` argv and the
  synchronous spawn/stdout/stderr/exit mapping used by the GTK helper. The GTK
  helper must not write TOML directly. The obsolete Zig Settings bridge source
  was removed during Phase 2 strict cleanup.

### Preview

- `preview/preview_commands.{c,h}` is the external command interface for
  availability, shortcuts, tool mapping, and execution.
- `preview/preview_geometry.{c,h}` owns color, point, and rectangle primitives.
  Production Preview callers no longer depend on the former Zig geometry ABI.
- `preview/preview_image_io.{c,h}` owns byte-for-byte file copies, PNG extension
  normalization, and asynchronous containing-folder launch. Returned paths are
  GLib-owned and released with `g_free`; `xdg-open` receives an explicit argv
  element without shell interpretation.
- `preview/preview_clipboard.{c,h}` owns Preview PNG/text clipboard publication.
  PNG copy resolves an executable sibling `shaula` before PATH fallback and
  invokes `clipboard copy-image --input <path> --json` with exact argv. Text copy
  sends owned bytes synchronously to `wl-copy --type text/plain` through stdin.
  Child stdout is suppressed; PNG stderr is captured and text stderr remains
  inherited. Borrowed inputs are never retained, and returned `GError` values
  are caller-owned.
- `preview/preview_notify.{c,h}` owns the Preview best-effort notification ABI.
  Required summary/body strings and the optional image path are borrowed for the
  synchronous call. It invokes `notify-send` through PATH with explicit argv,
  suppresses stdout/stderr, uses a bytewise escaped freedesktop image-path hint,
  retries with `-i` only when an image was supplied, and reports every build,
  spawn, nonzero-exit, or signal failure only as `FALSE` without `GError`.
- `preview/preview_result.{c,h}` owns parsing of the final JSON object emitted by
  the native Preview helper and the exact `close`, `copy`, `save`, `discard`, and
  `unknown` action tokens. Input is a borrowed length-bearing byte span; only
  outer ASCII space, tab, carriage return, and newline are trimmed. A JSON object
  is required, duplicate keys at any depth and malformed/trailing input are
  rejected, while unknown fields, unknown action strings, missing fields, and
  wrong-typed known fields retain compatibility defaults. A nonempty decoded
  `saved_path` is GLib-owned, may contain embedded NUL bytes, carries an
  authoritative length plus a trailing NUL, and is released through the public
  clear function. `preview/service.zig` copies it into the caller allocator and
  preserves existing `ERR_PREVIEW_RESULT_INVALID` mapping and CLI/post-capture
  JSON behavior.
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
- `preview/preview_paths.{c,h}` owns helper-side temporary capture-path
  recognition and must stay aligned with `runtime/paths.{c,h}`.

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
  width. More-menu actions use a consistent icon-label row and keep shortcuts in
  tooltips rather than widening individual rows. Numbered tool badges own the
  numeric slot; tooltips and overflow rows show only mnemonic letters when one
  exists. Contextual actions and HUD controls must not expand headerbar natural
  width.
- The Select contextual HUD is the region-redaction action surface. It must
  become visible immediately after a valid region drag, even when no annotation
  was intersected or another tool/property HUD was active, so Crop/Blur/Erase/
  Spotlight remain reachable without changing the headerbar width.
- New code comments are English and reserved for contracts, boundaries, or
  non-obvious decisions.
- Required validation after every change:

  ```bash
  ./dev check
  git diff --check
  ```

- C port changes additionally run:

  ```bash
  ./dev port-check
  ./dev port-check-asan
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
  v0.1.6 composition work must move immutable image pixels to shared assets
  before relying on multi-image documents and repeated snapshots.
- Most Preview geometry currently treats the base screenshot as the complete
  document bounds. Canvas work requires an explicit audit of render, export,
  crop, Spotlight, gestures, selection, paste placement, metadata, and fit.
- Preview GTK/C orchestration remains large; respect module ownership to avoid
  recreating command/action/state dependency cycles.
- Manual VM and compositor evidence is maintained outside this handoff. Do not
  convert historical observations into stronger support claims.
- QuickShell general integration and the static landing page are planned work,
  not part of the current capture/preview reliability scope.
- `./dev check` is green in the current checkout. Host-dependent Wayland,
  compositor, and external-tool assumptions still require explicit fixtures or
  manual evidence before stronger portability claims are made.
- The checkout was clean before this shared JSON slice. Historical Preview/UI
  and unrelated orchestration work remain outside the migration; this slice
  changed only JSON policy, direct caller adapters, build/tests, and ownership
  documentation.

## Immediate next steps

1. Continue Phase 4 with another explicitly selected pure-model or small-command
   slice; the shared JSON-envelope boundary is complete and must not absorb
   unrelated command orchestration.
2. Expand Phase 0 fixtures for CLI failures, config round trips, helper protocol
   outcomes, and remaining host-dependent tool assumptions.
3. Keep `preview/preview_paths.{c,h}` aligned with the runtime capture-artifact
   contract until the Preview boundary is consolidated.
4. Run `./dev check`, `./dev port-check`, `./dev port-check-asan`, and
   `git diff --check` after C migration changes.

## Relevant source documents

- `AGENTS.md` and `docs/agents/domain.md`: repository workflow and domain-doc
  policy.
- `spec/architecture.md`: public process, error, lifecycle, and ownership
  contracts.
- `spec/configuration.md`: public TOML schema, defaults, CLI, and Niri examples.
- `docs/adr/0001-preview-paste-architecture.md`: accepted async clipboard and
  image-ownership decision.
- `docs/adr/0002-port-zig-core-to-c.md`: accepted decision to remove Zig and
  converge on a C/Meson implementation.
- `spec/zig-to-c-port.md`: normative migration phases, compatibility gates,
  verification requirements, and C memory-management rules.
- `docs/port/baseline.md`: initial executable, dependency, fixture, and known
  failure baseline.
- `docs/port/migration-matrix.md`: grouped mapping from every Zig source/test
  module to callers, characterization, phase, and status.
- `docs/preview-tools.md`: detailed preview actions, selection, clipboard, tools,
  HUD persistence, and More-menu behavior.
- `docs/preview-ui-contract.md`: headerbar packing, overflow, startup readiness,
  selection chrome, and placement contracts.
- `docs/release-v0.1.5.md`: shipped release highlights, validation, and scope.
- `docs/plan-v0.1.6.md`: active release scope, milestones, emergent-work rules,
  quality gates, and release bar.
- `docs/plan-image-composition.md`: approved v0.1.6 product flow for stitching
  and composing images without global clipboard history.
- `docs/preview-expandable-canvas-design.md`: active v0.1.6 technical design for
  bounded canvas expansion, shared image assets, history, rendering, and
  geometry.
- `docs/releasing.md`: install/setup/release, icon packaging, and managed
  integration contracts.
- `docs/roadmap.md`: current release scope and non-goals.
- `scripts/qa/README.md`: maintained and manual QA lanes.
- `docs/wayland-runtime-test-plan.md`: manual runtime evidence plan. Preserve its
  claims until new direct testing is available.
