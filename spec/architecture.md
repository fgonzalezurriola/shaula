# Shaula Architecture Spec

See [spec/requirements.md](requirements.md) for product direction and [spec/algo.md](algo.md) for the locked engineering decisions.

## AGENT-FIRST CLI Contract

### Contract Versioning

- Contract identifier: `shaula-cli-contract`.
- `contract_version` is mandatory in every JSON response.
- Initial locked version for Task 3: `1.0.0`.
- Backward-compatible changes increment patch/minor, breaking changes increment major.

### Deterministic Machine-First Output Policy

- In `--json` mode, stdout emits exactly one valid JSON object.
- In `--json` mode, no human-only output is allowed on stdout.
- In `--json` mode, all machine-readable failures must include an `ERR_*` code.
- Field ordering in examples is canonical: `ok`, `contract_version`, `command`, `timestamp`, `result|error`, `warnings`.

### Deterministic CLI Grammar (v1)

```text
shaula <command-family> <command> [flags]

command-family := capture | preview | config | capabilities | history | clipboard | explore | settings | doctor | preflight | directory | setup | errors

capture command := quick | area | fullscreen | all-screens | window | previous-area
preview command := <file>
config command := show | init | niri-window-rule | niri-install
capabilities command := list
history command := list | show
clipboard command := copy-image | import-image
explore command := <none>
settings command := <none>
```

### Command Inventory and Required Flags

All commands below require `--json` for contract-compliant automation.

#### Capture family

- `shaula capture quick --json [--copy] [--save] [--preview|--no-preview] [--output <path>] [--dry-run]`
- `shaula capture area --json [--copy] [--save] [--preview|--no-preview] [--output <path>] [--dry-run]`
- `shaula capture fullscreen --json [--copy] [--save] [--preview|--no-preview] [--output <path>]`
- `shaula capture all-screens --json [--copy] [--save] [--preview|--no-preview] [--output <path>]`
- `shaula capture window --json [--copy] [--save] [--preview|--no-preview] [--output <path>] [--window-id <id>]`
- `shaula capture previous-area --json [--copy] [--save] [--preview|--no-preview] [--output <path>]`

#### Preview family

- `shaula preview <file> --json`

#### Config family

- `shaula config show --json`
- `shaula config init --json [--force] [--dry-run]`
- `shaula config niri-window-rule --json`
- `shaula config niri-install --json [--dry-run] [--path <path>]`

Preview defaults:

- `capture quick` and `capture area` launch post-capture preview by default.
- `capture fullscreen`, `capture all-screens`, `capture window`, and `capture previous-area` do not launch preview unless `--preview` is supplied.
- `--no-preview` disables preview explicitly. `--dry-run` never launches preview.

Copy defaults:

- `capture quick`, `capture area`, `capture fullscreen`, and `capture all-screens` copy to the clipboard by default.
- `capture window` and `capture previous-area` copy only when `--copy` is supplied.

#### Capabilities family

- `shaula capabilities list --json`

#### Explore family

- `shaula explore --json [--brief]`

`explore` is read-only desktop inventory for agent visual loops. It never
captures pixels or mutates desktop state. On Niri it normalizes outputs,
workspaces, windows, focused IDs, and a recommended capture target. Window
titles are visible desktop metadata and may contain sensitive user content.

#### Settings family

- `shaula settings`
- `shaula settings --json`

Plain `settings` launches the GTK settings UI. `settings --json` is the
machine-readable discovery entry point for agents: it explains the safe command
flow, privacy constraints, and JSON paths without opening UI or capturing
pixels.

#### History family

- `shaula history list --json`
- `shaula history show --json --id <entry_id>`

#### Clipboard family

- `shaula clipboard copy-image --json --input <path>`
- `shaula clipboard import-image --json [--output <path>]`

### Stable JSON Response Shape

#### Success response (top-level required fields)

Required fields: `ok`, `contract_version`, `command`, `timestamp`, `result`.

```json
{
  "ok": true,
  "contract_version": "1.0.0",
  "command": "capture area",
  "timestamp": "2026-04-18T20:45:00Z",
  "result": {
    "mode": "area",
    "path": "/tmp/shaula/captures/20260418-204500.png",
    "mime": "image/png",
    "preview": {
      "attempted": true,
      "ok": true,
      "error": null,
      "action": "close",
      "copied": false,
      "saved": false,
      "saved_path": null
    }
  },
  "warnings": []
}
```

Capture success responses also include a top-level `preview` object. Preview
helper failures are degraded post-capture state and do not change a successful
capture into an error response.

Preview command success responses include the final user action:

```json
{
  "ok": true,
  "contract_version": "1.0.0",
  "command": "preview",
  "timestamp": "2026-04-18T20:45:00Z",
  "result": {
    "path": "/tmp/shaula/captures/20260418-204500.png",
    "closed": true,
    "action": "copy",
    "copied": true,
    "saved": false,
    "saved_path": null
  },
  "warnings": []
}
```

#### Error response (top-level required fields)

Required fields: `ok`, `contract_version`, `command`, `timestamp`, `error`.

```json
{
  "ok": false,
  "contract_version": "1.0.0",
  "command": "capture window",
  "timestamp": "2026-04-18T20:45:01Z",
  "error": {
    "code": "ERR_WINDOW_TARGET_UNRESOLVED",
    "message": "window target could not be resolved",
    "retryable": false,
    "details": {
      "mode": "window"
    }
  },
  "warnings": []
}
```

### Error Taxonomy (explicit `ERR_*`)

#### Error Taxonomy

- Canonical machine-readable source is `shaula errors list --json`.
- Every failure must map deterministically as `error.code -> recovery action -> exit code`.
- Unknown classes must resolve to `ERR_UNKNOWN_UNMAPPED` with stable exit code (`99`).
- Retry policy is bounded and non-infinite: retryable classes map to a finite budget.
- Degraded behavior is explicit and class-scoped (e.g. clipboard/history failures degrade pipeline output instead of crashing capture).

#### Capture `ERR_*`

- `ERR_CAPTURE_MODE_UNSUPPORTED`
- `ERR_CAPTURE_BACKEND_UNAVAILABLE`
- `ERR_WINDOW_TARGET_UNRESOLVED`
- `ERR_CAPTURE_TIMEOUT`
- `ERR_PREVIOUS_AREA_UNAVAILABLE`

#### Runtime coordination `ERR_*`

- `ERR_IPC_TIMEOUT`
- `ERR_CAPTURE_IN_PROGRESS`

#### Capabilities `ERR_*`

- `ERR_UNSUPPORTED_COMPOSITOR`
- `ERR_PREFLIGHT_ENV_NOT_READY`
- `ERR_CAPABILITIES_PROBE_FAILED`

#### History `ERR_*`

- `ERR_HISTORY_STORE_UNAVAILABLE`
- `ERR_HISTORY_ENTRY_NOT_FOUND`

#### Clipboard `ERR_*`

- `ERR_CLIPBOARD_UNAVAILABLE`
- `ERR_CLIPBOARD_IMPORT_INVALID`
- `ERR_CLIPBOARD_COPY_FAILED`

#### Preview `ERR_*`

- `ERR_PREVIEW_INPUT_INVALID`
- `ERR_PREVIEW_UNAVAILABLE`

#### Config `ERR_*`

- `ERR_CONFIG_UNREADABLE`
- `ERR_CONFIG_INVALID`

## Process Topology

Shaula v1 uses direct CLI orchestration with short-lived helper processes and strict hot-path isolation.

### Process Boundaries

- **CLI process**: Parses commands, resolves capabilities, enforces guards, owns capture lifecycle orchestration, and emits the final JSON envelope.
- **Overlay helper**: Handles selection UI/input only and exits after producing deterministic selection output.
- **Capture helper/backend process**: Performs compositor or portal capture work and returns normalized capture metadata.
- **Preview/settings helpers**: Native GTK processes launched only when requested; they own their UI lifecycle and exit independently.
- **Runtime selection state**: Stores the last confirmed area geometry independently from history so `previous-area` stays on the short capture path.

### Hot-Path Isolation Rule

- Hot path is: `hotkey -> shaula CLI -> overlay selection (if needed) -> capture backend/helper -> artifact ready`.
- The capture path must work without plugin presence.
- Worker jobs and optional integrations must never block or delay hot-path completion.
- If a non-critical subsystem fails, the CLI returns deterministic `ERR_*` or degraded status while preserving capture completion when possible.

## Runtime Capture and Capability Enforcement

- Runtime capture backend success path must come from real runtime execution over a process boundary.
- Stub payload generation is allowed only for deterministic tests, never for productive runtime success.
- A forced stub backend (`SHAULA_CAPTURE_BACKEND=__stub__`) must fail deterministically with `ERR_CAPTURE_BACKEND_UNAVAILABLE` and must not leave output artifacts.
- `capabilities strict contract`: when `capabilities.capture.<mode>` is `false`, the related `capture <mode>` command must fail with `ERR_CAPTURE_MODE_UNSUPPORTED` before backend execution.
- Canonical backend identifier is `niri-wayland-direct` for both capabilities and capture result payloads.

## Capture Persistence and History Contract

- When `--output` is omitted and `--save` is not requested, capture writes an
  internal runtime artifact for preview/copy flows, not a user-visible saved
  screenshot.
- When `--save` is requested without `--output`, default capture output path is
  `~/Pictures/shaula` with `~/shaula` as the writable fallback.
- Invalid or non-writable explicit/default destinations resolve to deterministic
  `ERR_OUTPUT_PATH_INVALID`; copy-only runtime artifacts must not be reported as
  saved screenshots.
- History retention is Top-N 20 entries, newest-first, with deterministic trimming on write.
- `history show --id latest` resolves to the first retained entry and remains contract-stable.

## Configuration Contract

- Shaula resolves TOML config from `SHAULA_CONFIG_FILE`,
  `$XDG_CONFIG_HOME/shaula/config.toml`, then
  `$HOME/.config/shaula/config.toml`.
- Missing config is not an error and returns integrated defaults.
- Present unreadable config fails with `ERR_CONFIG_UNREADABLE`.
- Present invalid config fails with `ERR_CONFIG_INVALID`.
- Preview window identity is stable: app-id `dev.shaula.preview`, title
  `Shaula Preview`.
- `config niri-window-rule` renders a Niri `window-rule` for the preview app-id.
- `config niri-install` installs or replaces only Shaula's marked block in Niri
  config and creates a backup before changing an existing file.
- Shaula does not reload Niri automatically.

## Pre-Capture Guard Contract

- Before any capture backend execution, Shaula runs a precondition guard to avoid shell artifacts in resulting images.
- Guard behavior prefers explicit panel-hidden handshake when available.
- If handshake is unavailable, guard uses a bounded settle barrier fallback.
- Guard timeout maps to deterministic `ERR_CAPTURE_PRECONDITION_TIMEOUT`.
- Guard warning tokens are stable and machine-readable: `capture_precondition_panel_hidden_handshake`, `capture_precondition_settle_barrier`.

## Runtime Coordination Contract

Shaula has no resident daemon and no private Unix-socket command protocol. Each
CLI invocation resolves runtime state directly and launches only the helpers
needed for that command.

- `shaula doctor --json`, `shaula preflight --json`, and
  `shaula capabilities list --json` are the read-only health and discovery
  surfaces.
- Capture commands use a capture-session lock to reject overlapping hotkey
  invocations with `ERR_CAPTURE_IN_PROGRESS`.
- Helper process boundaries use command-specific argv, environment, and
  stdout/stderr contracts rather than a shared socket envelope.
- Helper timeouts map to deterministic command errors such as
  `ERR_IPC_TIMEOUT` where the external protocol itself timed out.
- Capture commands are never retried automatically because retries could create
  duplicate screenshots or unexpected portal prompts.

## Implementation Ownership Map

The public contracts above are implemented through these ownership seams. Keep
new work inside the existing owner instead of duplicating runtime decisions or
string contracts across modules.

### Capture command and lifecycle

- `capture/command_grammar.zig` owns capture flag membership and deterministic
  command-specific `ERR_CLI_USAGE` messages.
- `capture/command_flags.zig` declares per-mode flag structs and delegates
  parsing to the grammar.
- `capture/invocation.zig` converts parsed flags and resolved geometry into the
  lifecycle invocation contract: public token, backend operation, output/window
  fields, post-capture flags, previous-area persistence, and live-overlay settle
  behavior.
- `capture/command.zig` is a strict dispatcher into `capture/lifecycle.zig`.
- `capture/lifecycle.zig` owns capability enforcement, pre-capture guards,
  optional live-overlay settling, backend execution, previous-area persistence,
  and final success/error emission.
- `capture/backends/capture_execution_plan.zig` owns typed backend operations:
  `area`, `current_output`, `all_outputs`, and `window`.
- `capture/backends/capture_backend_failure.zig` centralizes backend
  `CaptureOutcome` failures while preserving deterministic `ERR_*` attributes.
- `capture/backends/capture_backend_contract.zig` owns public backend labels,
  degraded warning tokens, and helper exit-code mapping.
- `capture/warnings.zig` owns capture-specific warning tokens.

### Runtime and compositor decisions

- `capabilities/runtime.zig` owns backend/runtime selection and exposes the
  decision methods callers should use rather than comparing backend strings.
- `compositor/runtime.zig` owns compositor detection.
- `compositor/focused_output.zig` owns focused-output resolution.
- `runtime/env.zig` owns borrowed environment parsing.
- `runtime/tool_lookup.zig` owns fixed `grim` candidates and PATH-aware tool
  diagnostics.
- `runtime/process_exec.zig` owns shared process execution, including stdin-pipe
  execution. Callers still own output limits, cleanup, and deterministic error
  mapping.
- `runtime/helper_resolution.zig` owns helper lookup order: environment override,
  sibling binary, then PATH.

Backend execution receives an already resolved runtime decision and optional
focused output. It must not probe compositor/backend state again.

### Overlay

- `overlay/overlay.zig` is the public facade.
- `overlay/selection_session.zig` owns helper environment preparation, optional
  frozen backgrounds, dry-run/test payloads, helper protocol mapping, and
  accepted-selection persistence.
- `overlay/selection_draft_store.zig` owns persisted overlay draft state.
- `overlay/runtime.zig` owns the helper stdio process boundary.
- `overlay/helper_protocol.zig` owns parsing of the helper envelope.

### Post-capture and JSON

- `capture/post_capture.zig` owns post-capture side effects and typed outcomes
  for history, clipboard, and preview.
- `capture/post_capture_types.zig` owns post-capture state types.
- `capture/post_capture_json.zig` owns the stable capture result envelope,
  duplicated top-level/result fields, and partial/degraded rules.
- `cli/json.zig` owns shared timestamps, escaping, and deterministic JSON
  envelopes used by preview, history, errors, doctor, and notify commands.

### Diagnostics and configuration

- `doctor/diagnostics.zig` owns installed/runtime discovery.
- `config/save_args.zig` owns the `shaula config save` setting-flag grammar and
  applies flags to the config draft.
- `config/command.zig` owns command-level config flags, orchestration, and JSON
  envelopes.
- `settings/settings_config.zig` owns the settings configuration contract.

### Preview boundaries

- `preview/preview_paths.{c,h}` owns the helper-side temporary capture-path
  contract and must stay aligned with `runtime/paths.zig`.
- `preview/preview_geometry.zig` owns cross-language preview geometry and color
  conversion helpers.
- `preview/preview_image_io.zig` and `preview/preview_clipboard.zig` own preview
  image and clipboard runtime calls.
- `preview/preview_tool_defaults.{c,h}` owns per-tool last-used HUD defaults,
  tolerant INI loading, debounced dirty-key persistence, and cross-preview file
  locking. Inspector/widget state remains in `preview_properties_hud.*`.
- `runtime/c_compat.zig` owns C/GTK string and status compatibility glue;
  returned GLib strings remain GLib-owned and must be released with `g_free`.
- `ShaulaPreviewDocument` owns output-affecting preview model state. GTK widgets,
  view state, tools, gestures, and rendering remain in the C preview surface.

## Plugin Optionality Rule

- Noctalia integration is optional and non-blocking.
- Noctalia plugin availability must never be required for preflight or capture command execution.
- The plugin invokes validated Shaula CLI commands. Optional future shell IPC must remain bounded and outside core capture correctness.
- If Noctalia is absent or crashes, Shaula core capture flows remain fully operational and contract-compliant.

## Noctalia Optional Integration

### Scope (post-MVP, non-critical)

- Integration lives under `integrations/noctalia/shaula/` as an optional packaged bar widget.
- The widget invokes public Shaula CLI commands and does not implement screenshot logic itself.
- Integration is explicitly optional: capture core does not import, call, or wait for Noctalia plugin flows.

### Compatibility Contract

- Plugin actions map to public CLI commands such as `capture quick`,
  `capture area`, `capture fullscreen`, `capture all-screens`, `settings`, and
  `directory screenshots --open`.
- The widget owns only action-to-command translation; capture implementation
  remains exclusively in Shaula core.
- Capture commands keep their normal JSON and deterministic `ERR_*` contracts
  regardless of whether they were launched from a shell widget or a terminal.

### Failure Isolation and Non-Blocking Guarantees

- Plugin load or command-launch failures are isolated from the capture implementation.
- Capture hot path remains independent and works when the plugin is absent, disabled, or crashed.
- Noctalia-specific UI state must not add work to captures launched elsewhere.

### UI Backend Contract Rule
UI must only invoke validated CLI contracts; no direct backend capture calls.
