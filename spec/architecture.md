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

command-family := capture | preview | config | daemon | capabilities | history | clipboard

capture command := all-in-one | area | fullscreen | window | previous-area
preview command := <file>
config command := show | init | niri-window-rule | niri-install
daemon command := start | status | stop
capabilities command := list
history command := list | show
clipboard command := copy-image | import-image
```

### Command Inventory and Required Flags

All commands below require `--json` for contract-compliant automation.

#### Capture family

- `shaula capture all-in-one --json [--copy] [--save] [--preview|--no-preview] [--output <path>] [--dry-run]`
- `shaula capture area --json [--copy] [--save] [--preview|--no-preview] [--output <path>] [--dry-run]`
- `shaula capture fullscreen --json [--copy] [--save] [--preview|--no-preview] [--output <path>]`
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

- `capture area` and `capture all-in-one` launch post-capture preview by default.
- `capture fullscreen`, `capture focused`, `capture window`, and `capture previous-area` do not launch preview unless `--preview` is supplied.
- `--no-preview` disables preview explicitly. `--dry-run` never launches preview.

#### Daemon family

- `shaula daemon start --json [--socket <path>]`
- `shaula daemon status --json [--socket <path>]`
- `shaula daemon stop --json [--socket <path>]`

#### Capabilities family

- `shaula capabilities list --json`

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
    "path": "/tmp/shaula/capture-001.png",
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
    "path": "/tmp/shaula/capture-001.png",
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

#### Daemon `ERR_*`

- `ERR_DAEMON_ALREADY_RUNNING`
- `ERR_DAEMON_NOT_RUNNING`
- `ERR_IPC_BIND_FAILED`
- `ERR_IPC_TIMEOUT`

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

Shaula v1 uses a daemon-first, multi-process topology with strict hot-path isolation for Niri-first capture.

### Process Boundaries

- **Daemon (source of truth)**: The daemon is the source of truth for runtime state, capability decisions, and command orchestration.
- **Overlay process**: Handles selection UI/input only (pointer/keyboard, rectangle/aspect constraints) and exits after producing deterministic selection output.
- **Capture backend process**: Performs compositor/protocol capture operations (`area`, `fullscreen`, `window`) and returns normalized capture metadata.
- **Worker process(es)**: Execute asynchronous heavy tasks (long encoding, exports, uploads) outside the capture critical path.
- **UI process**: Desktop UI shell that consumes only validated daemon/CLI contracts and never bypasses daemon control.
- **Runtime selection state**: Stores the last confirmed area geometry independently from history so `previous-area` stays on the short capture path.

### Hot-Path Isolation Rule

- Hot path is: `hotkey -> daemon dispatch -> overlay selection (if needed) -> capture backend -> artifact ready`.
- The capture path must work without plugin presence.
- Worker jobs and optional integrations must never block or delay hot-path completion.
- If a non-critical subsystem fails, daemon returns deterministic `ERR_*` / degraded status while preserving capture completion when possible.

## Runtime Capture and Capability Enforcement

- Runtime capture backend success path must come from real runtime execution over a process boundary.
- Stub payload generation is allowed only for deterministic tests, never for productive runtime success.
- A forced stub backend (`SHAULA_CAPTURE_BACKEND=__stub__`) must fail deterministically with `ERR_CAPTURE_BACKEND_UNAVAILABLE` and must not leave output artifacts.
- `capabilities strict contract`: when `capabilities.capture.<mode>` is `false`, the related `capture <mode>` command must fail with `ERR_CAPTURE_MODE_UNSUPPORTED` before backend execution.
- Canonical backend identifier is `niri-wayland-direct` for both capabilities and capture result payloads.

## Capture Persistence and History Contract

- When `--output` is omitted, default capture output path is `~/Pictures/Shaula`.
- Invalid or non-writable default destination resolves to deterministic `ERR_OUTPUT_PATH_INVALID`, no silent fallback to `/tmp`.
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

## IPC Contract v1

### Transport and Socket Path Rules

- Transport: Unix domain stream socket (local-only IPC).
- Default socket path: `${XDG_RUNTIME_DIR}/shaula/daemon-v1.sock`.
- If `XDG_RUNTIME_DIR` is unavailable, fallback path is `/tmp/shaula-${UID}/daemon-v1.sock`.
- Socket path is configurable via CLI `--socket <path>` for daemon commands.
- Daemon start must fail with `ERR_IPC_BIND_FAILED` when bind path is invalid, unavailable, or already bound by a non-compatible endpoint.

### Versioning and Compatibility

- IPC protocol identifier: `shaula-ipc`.
- `ipc_version` is mandatory in every request and response envelope.
- Locked v1 value: `1.0.0`.
- Backward-compatible additive changes are minor/patch; incompatible envelope/semantic changes require major bump and new socket suffix (`daemon-v2.sock`).

### Request / Response Envelope

#### Request envelope (required fields)

```json
{
  "ipc_version": "1.0.0",
  "request_id": "req-01HZX...",
  "command": "capture.area",
  "deadline_ms": 1500,
  "payload": {},
  "client": {
    "name": "shaula-cli",
    "version": "1.0.0"
  }
}
```

#### Response envelope (required fields)

```json
{
  "ipc_version": "1.0.0",
  "request_id": "req-01HZX...",
  "ok": true,
  "result": {},
  "error": null,
  "degraded": false,
  "daemon_state": "ready",
  "warnings": []
}
```

### Timeout and Retry Policy

- Client default request timeout: `1500ms` for hot-path operations; non-hot-path operations may opt into higher deadlines.
- Daemon returns `ERR_IPC_TIMEOUT` when request processing exceeds `deadline_ms`.
- Retry policy for idempotent non-capture commands (`daemon.status`, `capabilities.list`): up to 2 retries with fixed 75ms backoff.
- No automatic retry for capture commands to avoid duplicate captures and latency spikes.

### Health Checks and Liveness

- `daemon.status` is the canonical health command and must be served without overlay/backend startup side effects.
- Health response includes daemon state (`initializing`, `ready`, `capturing`, `degraded`, `error`) and active IPC version.
- A successful status call confirms IPC handshake, envelope version compatibility, and event-loop responsiveness.

## Plugin Optionality Rule

- Noctalia integration is optional and non-blocking.
- Noctalia plugin availability must never be required for daemon startup, preflight, or capture command execution.
- Plugin communication must run through optional IPC adapters with bounded timeouts; adapter failures only produce warnings/degraded metadata.
- If Noctalia is absent or crashes, Shaula core capture flows remain fully operational and contract-compliant.

## Noctalia Optional Integration

### Scope (post-MVP, non-critical)

- Integration lives under `integrations/noctalia/*` as a Proof of Concept (PoC).
- The PoC is daemon-connected through the same versioned IPC envelope (`ipc_version: 1.0.0`) used by core daemon contracts.
- Integration is explicitly optional: capture core does not import, call, or wait for Noctalia plugin flows.

### Compatibility Contract

- Plugin requests must include `ipc_version`, `request_id`, `command`, and bounded `deadline_ms`.
- Plugin-side MVP action adapter maps exactly these actions to Shaula CLI contracts: `capture-area`, `capture-fullscreen`, `capture-window`, `open-last`, `history`.
- Adapter owns action-to-command translation, while capture implementation remains exclusively in Shaula core.
- Daemon responses are validated by plugin adapter before exposing plugin-triggered action results.

### Failure Isolation and Non-Blocking Guarantees

- Plugin failures (`ERR_NOCTALIA_IPC_UNAVAILABLE`, invalid response, version mismatch) are isolated from capture path.
- Capture hot path remains independent and must complete even when plugin is absent, disabled, or crashed.
- Runtime policy: plugin adapter timeouts are bounded (`<=250ms` in PoC scripts) and never on capture hot path.

### Added-Latency Budget (PoC)

- A dedicated benchmark (`scripts/qa/benchmark-plugin-overhead.sh`) measures baseline daemon status path vs plugin-mediated path.
- Acceptance gate for Task 15: added p95 latency from plugin path must remain within configured threshold (`--max-added-p95-ms`, default test gate: `15ms`).

### UI Backend Contract Rule
UI must only invoke validated CLI/daemon contracts; no direct backend capture calls.
