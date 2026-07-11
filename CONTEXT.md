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
- The port specification and decision record remain in
  `spec/zig-to-c-port.md` and `docs/adr/0002-port-zig-core-to-c.md`. They describe
  migration constraints and history, not current mixed-language ownership.
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

- `src/main.c` owns top-level command dispatch and the public JSON command
  envelopes.
- `src/capture/command.c` owns capture grammar, capability guards, session
  locking, overlay selection, backend invocation, artifact validation, history,
  clipboard, Preview launch, and capture result JSON.
- `src/config/config.c` owns defaults, the supported TOML subset, config path
  resolution, validation, comment/layout-preserving field updates, timestamped
  backup, and atomic save. Canonical serialization is used for new files.
- `src/main.c` also owns managed Niri preview/keybind block rendering and
  installation. Managed writes preserve surrounding user KDL, reject malformed
  markers with deterministic `ERR_CONFIG_INVALID`, back up changed files, and
  are idempotent.
- `src/runtime/` owns environment parsing, runtime paths, tool/helper lookup,
  process execution, previous-area state, and capture-session locking.
- `src/compositor/`, `src/capabilities/`, and `src/preflight/` own compositor
  classification, focused-output discovery, runtime/backend decisions, and the
  complete preflight response. `src/explore/inventory.c` queries Niri and
  normalizes outputs, workspaces, windows, focused IDs, and capture advice.
- `src/errors/taxonomy.c` is the canonical `ERR_*` table and exit mapping.
  `src/cli/json.c` owns shared JSON escaping, timestamps, warnings, and basic
  error envelopes.
- `src/main.c` preserves the history list/show, clipboard state import/copy,
  notification test/action-listener, and file-reveal command contracts.
- `src/preview/`, `src/settings/`, and `src/overlay/` contain the native GTK
  helpers and their C support modules.

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

## Verification

`./dev check` also runs the non-intrusive top-level port command compatibility
fixture covering capture selection, config preservation, clipboard/history,
Explore inventory, notification grammar, and overlay error details.

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
