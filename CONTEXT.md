# Context

Compact engineering handoff for Shaula. `./dev context` copies this file, the
last three commits, and the working diff. Git remains the implementation
history; this document records current behavior and ownership.

## Current focus

- v0.1.5 is the latest released baseline. v0.1.6 remains the image composition
  and expandable canvas release described in `docs/plan-v0.1.6.md`.
- The Zig-to-C migration is cut over. Production, tests, QA, packaging, and CI
  use Meson and C; the repository has no maintained Zig source or Zig build
  metadata.
- The port specification, ADR, baseline, and migration matrix remain as
  historical records. Active architecture, testing, release, packaging, and
  contributor documentation describe Meson and C as the only maintained path.
- Interactive Wayland/Niri validation is still required whenever capture,
  overlay, clipboard, GTK, or compositor behavior changes.

## Build and install ownership

- `meson.build` is the sole production build definition.
- `./dev build`, `./dev check`, `./dev release`, `./dev port-check`, and
  `./dev port-check-asan` are the supported developer entry points.
- Meson builds and installs six executables: `shaula`, `shaula-overlay`,
  `shaula-preview`, `shaula-settings`, `shaula-crop-image`, and
  `shaula-portal-screenshot`.
- The install also includes the hicolor icon tree and the Noctalia integration.
- `./dev dev-install --yes` stages the Meson install, makes a local release
  archive, invokes `scripts/install.sh`, and reloads Noctalia when applicable.
- GitHub release and C-port workflows build, test, stage, and inspect the Meson
  payload. AUR source packaging uses Meson/Ninja. JSON-GLib is the maintained
  runtime/build dependency for normalized Niri Explore inventory.

## Runtime ownership

- `src/main.c` is a dispatch table only. `src/commands/` owns the non-capture
  command families, shared command JSON/error adaptation, and each family's
  public envelope. `src/capture/command.c` remains the deep capture command
  interface.
- `src/capture/command.c` owns capture grammar, capability guards, overlay
  selection, backend invocation, artifact validation, history, clipboard,
  Preview launch, and capture result JSON. Capture-specific runtime filenames,
  locking, temporary directories, and previous-area persistence stay behind
  `src/runtime/capture_state.{c,h}`.
- Capture session ownership is lifecycle-scoped: every early outcome releases
  automatically, while the success path explicitly releases before Preview.
- `src/config/config.c` owns defaults, the supported TOML subset, config path
  resolution, validation, comment/layout-preserving field updates, timestamped
  backup, and atomic save. Canonical serialization is used for new files.
- `src/config/niri_managed.c` owns managed Niri preview/keybind path resolution,
  marker validation, preservation, backups, atomic replacement, removal, and
  conflict scanning; `src/commands/config_command*.c` owns rendering and public
  JSON presentation. Managed writes preserve surrounding user KDL, reject malformed
  markers with deterministic `ERR_CONFIG_INVALID`, back up changed files, and
  are idempotent.
- `src/runtime/` builds as one deep runtime module. `helper_resolution.{c,h}`
  is the executable-discovery interface for current-binary, helper sibling,
  fixed-candidate, and `PATH` lookup. `process_exec.{c,h}` is the sole
  synchronous direct-argv process interface. `capture_state.{c,h}` owns
  capture-specific runtime paths, the exclusive session, overlay artifacts,
  and previous-area persistence; its path/lock/store modules are implementation
  details.
- `src/compositor/`, `src/capabilities/`, and `src/preflight/` own compositor
  classification, focused-output discovery, runtime/backend decisions, and the
  complete preflight response. `src/explore/inventory.c` queries Niri and
  normalizes outputs, workspaces, windows, focused IDs, and capture advice.
- `src/errors/taxonomy.c` is the canonical `ERR_*` table and exit mapping.
  `src/cli/json.c` owns shared JSON escaping, timestamps, warnings, and basic
  error envelopes.
- `src/commands/history_command.c`, `clipboard_command.c`, and
  `notify_command.c` preserve history list/show, clipboard state import/copy,
  notification test/action-listener, and file-reveal command contracts.
- `src/preview/`, `src/settings/`, and `src/overlay/` contain the native GTK
  helpers and their C support modules.
- `src/overlay/overlay_selection_session.{c,h}` owns selection interaction
  ordering, drag/hover modes, handle resolution, aspect application, keyboard
  nudging, semantic cursors, and release confirmation. `overlay_selection.c`
  remains its bounded geometry implementation; GTK callbacks only adapt input
  and render the session view.
- `src/preview/preview_edit_session.{c,h}` owns annotation queries, edit
  begin/finish policy, selection clearing, undo/redo snapshots, gesture history,
  and operation cancellation. Annotation variant rules and ADR-0001 Image
  ownership remain in `preview_annotations.c`; GTK files are edit-session
  adapters.
- `src/settings/settings_config.{c,h}` is the Settings configuration protocol.
  It derives UI defaults from `ShaulaConfig` and owns public config JSON,
  `config save` flag mapping, and the Settings save argv. `settings_process.c`
  owns process execution only.
- Preview Text drafts live in `preview_canvas.c` (`text_entry` +
  `SHAULA_OPERATION_TEXT`). Committed Text can be reopened for string editing
  via a no-drag second click when singly selected, or by clicking it with the
  Text tool. `text_editing_id` tracks re-edit vs create; empty re-edit deletes.

## Public capture behavior

- Public modes are `quick`, `area`, `fullscreen`, `all-screens`, `window`, and
  `previous-area` where runtime capabilities permit them. `focused` and
  `all-in-one` remain compatibility aliases.
- Quick and Area use the overlay helper protocol. `Ctrl+C` confirms with copy;
  `Ctrl+S` confirms with durable save; normal confirmation follows per-mode
  config.
- `fullscreen` targets the focused output. `all-screens` targets the compositor
  layout. `window` is capability-gated.
- Capture uses an exclusive runtime session lock. Concurrent invocations return
  `ERR_CAPTURE_IN_PROGRESS` and the lock is released before Preview.
- Implicit artifacts live under the runtime capture directory. `--save` writes
  to the configured save folder. PNG output is validated before side effects.
- Runtime backend selection is deterministic: explicit override, forced portal,
  native compositor lane, then supported portal fallback.
- Region mode accepts `live` and `frozen`. The helper receives the frozen-mode
  contract; live mode observes the compositor settle barrier before capture.
  Frozen-source immutability remains a manual Wayland/Niri release gate.

## Configuration and integrations

- The public config file is `${XDG_CONFIG_HOME:-~/.config}/shaula/config.toml`.
  Missing nullable floating coordinates remain absent during serialization.
- Settings persists only through `shaula config save --json`; it does not own a
  second format.
- Niri preview rules use `// BEGIN/END SHAULA PREVIEW WINDOW RULE` markers.
- Niri shortcuts use `// BEGIN/END SHAULA MANAGED KEYBINDS` markers and invoke
  `shaula capture <mode> --json` directly with no shell.
- `shaula setup` never replaces an existing valid Shaula config with defaults.
- `scripts/install.sh` owns release extraction, desktop/icon installation, the
  Noctalia file/state integration, and then invokes `shaula setup` for config
  and Niri integration.

## Stable JSON and error contracts

- Public command envelopes use contract version `1.0.0`.
- JSON commands require `--json`; unsupported grammar returns `ERR_CLI_USAGE`.
- Known errors map through `src/errors/taxonomy.c`; unknown codes collapse to
  `ERR_UNKNOWN_UNMAPPED` and exit 99.
- Helper stdout is a protocol boundary. Diagnostic text belongs on stderr and
  malformed helper payloads map to deterministic `ERR_*` outcomes.
- Environment spans and static taxonomy records are borrowed. GLib-owned result
  buffers must be cleared by their documented clear functions.
- An empty persisted `capture.after.save_folder` retains the historical default
  `~/Pictures/shaula`; forced-save shortcuts must not treat it as an invalid path.
- Direct save captures emit detached success notifications, gated by
  `notifications.success`; thumbnail inclusion is gated by `notifications.thumbnails`.

## Verification

`./dev check` also runs the non-intrusive top-level port command compatibility
fixture covering capture selection, config preservation, clipboard/history,
Explore inventory, notification grammar, and overlay error details.
The host-sensitive `contract` suite remains local QA and is excluded from the
headless GitHub C-build matrix.

Every code change must run:

```bash
./dev check
git diff --check
```

The fast non-intrusive product matrix is:

```bash
./dev qa
```

Strict compiler and sanitizer lanes are:

```bash
./dev port-check
./dev port-check-asan
```

For interactive overlay behavior, the user validates:

```bash
./dev capture
./dev all
```

## Active product direction

- Preview image composition and bounded canvas expansion remain v0.1.6 scope.
- `Ctrl+Shift+V` is the system-clipboard paste command; preview-local annotation
  paste remains `Ctrl+V`.
- Scrolling capture, recording, OCR, upload/share, persistent pinning, smart
  selection, AI removal, infinite canvas, and global clipboard history remain
  out of scope.
