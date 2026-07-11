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
- `errors/taxonomy.{c,h}` owns the current 28-entry inventory, exact ordering,
  messages, retryability, classes, recovery actions, exit codes, and retry
  budgets. `src/main.c` owns only command parsing and enumeration of that
  table; shared contract-version, timestamp, escaping, and basic-error policy is
  provided by `cli/json.{c,h}`.
- Every failure must map deterministically as `error.code -> recovery action -> exit code`.
- Unknown, malformed, or otherwise unmapped codes resolve to the exact
  `ERR_UNKNOWN_UNMAPPED` record with stable exit code `99` and retry budget `0`.
- Retry policy is bounded and non-infinite: retry-limited records use budget `3`,
  degrade-to-portal uses budget `1`, and all others use `0`.
- Taxonomy lookup is explicit-length and byte-exact. It does not trim, fold case,
  accept prefixes/suffixes, normalize Unicode, or truncate at embedded NUL.
- Static taxonomy records and class/action tokens are borrowed immutable
  process-lifetime literals. Failure-class and recovery-action values cross the
  Zig/C boundary as asserted 32-bit integers.
- `ERR_PREVIEW_RESULT_INVALID` is emitted by Preview service but is intentionally
  absent from the current compatibility fixture, so it preserves the existing
  unknown fallback and exit code `99`. `ERR_CAPABILITIES_PROBE_FAILED` remains a
  reserved architecture token rather than an invented table row until a runtime
  caller and compatibility contract exist.
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

- `core/capture_mode.{c,h}` owns exact public and region tokens, fixed-width ABI
  values, runtime/backend lane mapping, and interactive-selection policy.
  Maintained C callers include the header directly and keep only immediate
  caller-local span/status adaptation.
- `src/capture/command.c` owns capture flag membership and deterministic
  command-specific `ERR_CLI_USAGE` messages.
- `src/capture/command.c` declares per-mode flag structs and delegates
  parsing to the grammar.
- `src/capture/command.c` converts parsed flags and resolved geometry into the
  lifecycle invocation contract: public token, backend operation, output/window
  fields, post-capture flags, previous-area persistence, and live-overlay settle
  behavior.
- `src/capture/command.c` is a strict dispatcher into `src/capture/command.c`.
- `src/capture/command.c` owns capability enforcement, pre-capture guards,
  optional live-overlay settling, backend execution, previous-area persistence,
  and final success/error emission.
- `src/capture/command.c` owns typed backend operations:
  `area`, `current_output`, `all_outputs`, and `window`.
- `src/capture/command.c` centralizes backend
  `CaptureOutcome` failures while preserving deterministic `ERR_*` attributes.
- `capabilities/runtime.{c,h}` owns canonical public backend labels.
  `src/capture/command.c` retains degraded warning tokens
  and helper exit-code mapping.
- `src/capture/command.c` owns capture-specific warning tokens.

### Runtime and compositor decisions

- `capabilities/runtime.{c,h}` owns fixed-width backend/runtime selection,
  portal capability probing, mode support, fallback ordering, canonical backend
  labels, and decision-policy helpers. Maintained callers include the C header
  directly.
- `compositor/runtime.{c,h}` owns pure compositor environment detection,
  classification, stable kind tokens, and support/overlay policy. Maintained
  callers include the C header directly.
- `compositor/focused_output.{c,h}` owns focused-output overrides, exact
  Niri/Sway process probes, typed result parsing, and advisory fallback.
- `preflight/probe.{c,h}` owns aggregate preflight resolution, readiness guard
  precedence, exact public success/error JSON, warnings, and exit-code mapping.
  The top-level Zig dispatcher includes the header directly.
- `runtime/env.{c,h}` owns borrowed environment parsing through direct C-header
  callers and caller-local span conversion.
- `runtime/paths.{c,h}` owns runtime-state path resolution, temporary capture
  classification, and parent creation through direct C-header callers.
- `runtime/tool_lookup.{c,h}` owns fixed `grim` candidates, first-existing
  absolute lookup, PATH-aware diagnostics, and generic existence checks through
  direct C-header callers.
- `runtime/helper_resolution.{c,h}` owns helper resolution precedence and
  existence-only sibling checks through direct C-header callers.
- `runtime/previous_area_store.{c,h}` owns previous-area serialization, parsing,
  synchronous state-file I/O, and backend support gating through direct C-header
  callers.
- `runtime/capture_session_lock.{c,h}` owns exclusive acquisition, exact PID
  contents, bounded stale-owner detection, one-shot replacement, and best-effort
  release through direct C-header callers. Capture lifecycle still owns releasing
  the gate before post-capture Preview work.
- `runtime/process_exec.{c,h}` owns shared direct-argv process execution through
  direct C-header callers: parent-PATH lookup, replacement environments,
  concurrent bounded stdout/stderr capture, binary stdin, termination mapping,
  and child cleanup. Callers still own explicit output limits, returned-buffer
  cleanup, and deterministic command-specific error mapping.

Backend execution receives an already resolved runtime decision and optional
focused output. It must not probe compositor/backend state again.

### Compositor runtime boundary

- Compositor kinds cross the C ABI as fixed 32-bit values: Niri `0`, Wayland
  `1`, and unsupported `2`. Stable kind-token spans borrow immutable
  process-lifetime literals.
- Detection precedence is exact: nonempty ASCII-trimmed `SHAULA_COMPOSITOR`,
  presence of `NIRI_SOCKET`, the first nonempty `XDG_CURRENT_DESKTOP` token
  split on `:` or `;`, nonempty ASCII-trimmed `XDG_SESSION_DESKTOP`, presence
  of `WAYLAND_DISPLAY`, then the canonical `unsupported` fallback.
- Empty `NIRI_SOCKET` and `WAYLAND_DISPLAY` values remain present. Empty or
  ASCII-whitespace-only explicit/session values fall through. The first desktop
  token is decisive even when unsupported; later tokens and fallbacks are not
  consulted.
- Niri comparison is ASCII case-insensitive and canonicalizes the label to
  `niri`. Exact known Wayland and wlroots tokens are ASCII case-insensitive, but
  non-Niri labels preserve their original bytes and case. The historical
  generic substring rule recognizes only a literal lowercase `wayland`
  substring.
- Explicit classification consumes borrowed `data + length` spans. NULL plus
  zero length is an empty label; NULL plus nonzero length is invalid. Embedded
  NUL, invalid UTF-8, non-ASCII bytes, prefixes, and suffixes are observed
  byte-for-byte without normalization or locale-sensitive parsing.
- Current-scope support remains Niri or wlroots, plus generic Wayland only when
  the portal is available. Overlay support remains Niri or wlroots. Backend
  selection remains typed and separate in `capabilities/runtime.{c,h}`.
- The C module allocates nothing, performs no filesystem/process/shell work, and
  has no mutable global state. Returned noncanonical labels borrow the caller's
  environment or classification input; callers must not retain them after that
  storage changes.
- Capabilities, preflight, and explore perform caller-local environment lookup
  and immediate span/enum conversion. The focused-output C boundary receives the
  same borrowed environment values and reuses the detector synchronously. No
  C compositor-policy boundary remains.

### Capability-runtime boundary

- Backend kinds cross the C ABI as fixed 32-bit values: Niri direct `0`,
  grim/wlroots `1`, portal `2`, and stub `3`. Canonical backend labels borrow
  immutable process-lifetime storage.
- `ShaulaCapabilitiesEnvironment` borrows the five compositor environment values
  plus backend, force-portal, portal-available, and portal-window overrides for
  one synchronous call. The C module does not read those variables from
  process-global state.
- Selection precedence is exact: recognized ASCII-trimmed backend override,
  enabled force-portal flag, Niri direct, wlroots grim/portal policy, generic
  Wayland with portal, then the historical portal default. Unknown backend
  override values fall through.
- Wlroots checks the fixed grim candidates. When grim is absent it selects portal
  if available and otherwise retains grim so backend execution produces the
  established unavailable-backend outcome rather than changing policy.
- Valid portal availability overrides are authoritative. Otherwise the module
  runs the exact `gdbus` Properties.Get calls for `version` and
  `AvailableTargets`, with 2048-byte stdout and stderr limits. Spawn, lookup,
  stream-limit, exit, signal, and parse failures are advisory portal absence and
  never become public `ERR_*` failures.
- The last unsigned decimal value from `AvailableTargets` enables window
  capability for window bit `2` or active-window bit `8`. Portal availability
  and window capability remain separate result fields.
- Supported non-stub decisions expose area, fullscreen, and all-screens; window
  remains false. Unsupported compositors and stub decisions expose no modes.
  Mode parsing is exact and case-sensitive and shares the C capture-mode table.
- Niri direct, grim, and stub expose portal as their only ordered fallback;
  portal exposes none. Helper functions own degraded-backend, overlay-bypass,
  portal-selection, previous-area, and portal-fallback policy.
- `ShaulaRuntimeDecision` owns no allocations. Its compositor label borrows the
  caller environment; backend labels are immutable. Independent Zig `@cImport`
  namespaces exchange the fixed-layout decision only through caller-local
  field-by-field ABI conversion, never through a C policy boundary.

### Preflight boundary

- Preflight status values cross the C ABI as fixed 32-bit integers: success `0`,
  invalid argument `1`, size overflow `2`, out of memory `3`, timestamp out of
  range `4`, and internal dependency failure `5`.
- `shaula_preflight_build()` borrows one `ShaulaCapabilitiesEnvironment`, one
  caller-provided Unix timestamp, and one explicit-length portal-fallback warning
  span for a synchronous call. It does not read process-global environment state
  or acquire the clock itself.
- Guard precedence is exact: an unsupported runtime decision emits
  `ERR_UNSUPPORTED_COMPOSITOR` before Wayland readiness is considered. Supported
  decisions then require presence of `WAYLAND_DISPLAY`; a present empty value is
  ready, while absence emits `ERR_PREFLIGHT_ENV_NOT_READY`.
- Unsupported and environment-not-ready results preserve their exact messages,
  retryability, escaped `detected_compositor` details, empty warning array, and
  taxonomy exit codes 10 and 11 respectively.
- Success preserves canonical public field order and the historical duplicated
  compositor value: `ok`, `contract_version`, `command`, `timestamp`,
  `compositor`, `ready`, `result`, and `warnings`. The result includes
  compositor, `wayland=true`, canonical backend label, and portal availability.
- The borrowed portal-fallback warning is serialized only when the resolved
  backend is portal. Preflight does not duplicate backend-selection or warning-
  escaping policy.
- The boundary reuses `cli/json.{c,h}` for contract version, timestamps,
  byte-string escaping, warning arrays, and basic errors, and
  `errors/taxonomy.{c,h}` for exit-code mapping.
- A successful `ShaulaPreflightOutput` owns GLib-allocated, length-bearing JSON
  with trailing-NUL storage plus the command exit code. It must be released with
  `shaula_preflight_output_clear()`; replacement, partial failure, and repeated
  cleanup leave the structure valid and empty.
- `src/main.c` includes `preflight/probe.h` directly and retains only flag dispatch,
  environment adaptation, testable clock acquisition, stdout writing, and
  returning the C-provided exit code. No maintained C preflight boundary remains.

### Focused-output boundary

- Focused-output status values cross the ABI as fixed 32-bit integers: success
  `0`, invalid argument `1`, and final-result out of memory `2`.
- `ShaulaFocusedOutputEnvironment` borrows the caller-supplied
  `SHAULA_OVERLAY_OUTPUT_NAME` value and the five compositor environment values
  for one synchronous call. The module does not read or mutate process-global
  environment state itself.
- A nonempty ASCII-trimmed output override wins without compositor detection or
  child-process execution. Missing, empty, and whitespace-only overrides fall
  through to compositor-specific probing.
- Niri executes exactly `niri msg -j focused-output`, with stdout limited to
  8192 bytes and stderr to 1024 bytes. Sway executes exactly
  `swaymsg -t get_outputs -r`, with stdout limited to 65536 bytes and stderr to
  1024 bytes. Other compositors return no output without spawning a process.
- Spawn and lookup errors, stream-limit failures, nonzero or signaled exits,
  empty output, malformed JSON, and incomplete typed results are advisory
  absence. Focused-output probing never creates a public `ERR_*` error and does
  not own backend selection or command-level fallback decisions.
- The Niri result is one object with required nonempty string `name`. The Sway
  result is one array of objects with required string `name` and optional boolean
  `focused`, defaulting false; the first focused nonempty name wins, but every
  later item is still validated. Unknown fields are validated and ignored.
- Decoded known keys are unique, including escaped spellings. Wrong known types,
  missing required fields, invalid UTF-8, raw control bytes, malformed escapes,
  unpaired surrogates, malformed numbers, wrong root types, and trailing input
  invalidate the probe. Escaped NUL and valid surrogate pairs are decoded into
  the explicit-length output exactly.
- Probe-process and parser allocation failures remain advisory absence, matching
  the former best-effort Zig boundary. Allocation of the final selected name is
  the only out-of-memory status propagated to a caller.
- A present result is an independent GLib-owned byte buffer with authoritative
  length and trailing-NUL storage. It may contain an embedded NUL and must be
  released through `shaula_focused_output_result_clear()`. Replacement and
  repeated cleanup are safe.
- Explore, capture lifecycle, and overlay selection include the C header
  directly, pass their own environment maps, copy only a present result into
  their established caller allocation, and retain their existing advisory or
  backend-level fallback behavior. No maintained C focused-output boundary
  remains.

### Overlay

- `src/capture/command.c` owns overlay orchestration; `src/overlay/` owns the native helper.
- `src/capture/command.c` and `src/overlay/` owns helper environment preparation, optional
  frozen backgrounds, dry-run/test payloads, helper protocol mapping, and
  accepted-selection persistence.
- `src/capture/command.c` and `src/overlay/` owns persisted overlay draft state.
- `src/capture/command.c` and `src/overlay/` owns the helper stdio process boundary.
- `src/capture/command.c` and `src/overlay/` owns parsing of the helper envelope.

### Post-capture and JSON

- `src/capture/command.c` owns post-capture side effects and typed outcomes
  for history, clipboard, preview, and notifications.
- `src/capture/command.c` owns post-capture state types.
- `src/capture/command.c` owns the stable capture result envelope,
  duplicated top-level/result fields, and partial/degraded rules.
- `cli/json.{c,h}` owns the shared contract-version literal, byte-string
  escaping, warning-array serialization, UTC timestamp formatting, and complete
  basic-error envelope. Maintained Zig commands include the C header directly
  and keep only caller-local ABI/allocator/writer adaptation.
- `notify/request.{c,h}` owns notification request defaults, urgency tokens,
  exact `notify-send` argv construction, action spelling, and file-URI escaping.
  `src/main.c` owns execution, fallback, action listening, reveal behavior, and
  caller-local C span/status/argv adaptation.

### Public JSON boundary

- `shaula_json_contract_version` returns borrowed immutable process-lifetime
  bytes for public contract version `1.0.0`. `src/cli/json.c` owns only the
  independent IPC version.
- Every C JSON input is an explicit `data + length` span. NULL with zero length
  means an empty byte string; NULL with nonzero length is invalid. Arbitrary
  bytes are never silently interpreted as NUL-terminated text.
- JSON escaping is byte-oriented and preserves the previous default Zig
  stringifier behavior. Quote and backslash are escaped; slash is unchanged;
  backspace, form feed, tab, carriage return, and newline use short escapes;
  every other byte `0x00..0x1f` uses lowercase `\\u00xx`; and every other byte
  is copied unchanged. Valid UTF-8, invalid UTF-8, and non-ASCII bytes therefore
  pass through unchanged, while embedded NUL becomes `\\u0000`.
- Nullable strings preserve absence as JSON `null` and a present zero-length
  span as JSON `""`; callers must not collapse those states.
- Timestamp formatting consumes caller-supplied Unix seconds and emits exactly
  `YYYY-MM-DDTHH:MM:SSZ`. Callers still obtain the clock through their existing
  `std.Io` boundary before crossing the ABI, preserving test-clock behavior.
- Successful C builders return GLib-owned length-bearing bytes with a trailing
  NUL for convenience. The authoritative length may include escaped source NULs;
  callers clear outputs with `shaula_json_owned_bytes_clear`. Repeated clear and
  replacement are safe. Callers that retain bytes in Zig copy them into their
  existing allocator before C cleanup.
- The shared basic error preserves canonical order
  `ok, contract_version, command, timestamp, error, warnings`, writes one complete
  object with exactly one final newline, and never writes stderr. Its borrowed
  `details_json` fragment is inserted verbatim and is intentionally not parsed or
  validated, preserving the previous compatibility boundary.
- Command-specific serializers remain responsible for typed result and detail
  fields: capture and post-capture state, config details, history records,
  capabilities, doctor/explore inventory, errors-list records, Preview results,
  directory/clipboard results, settings discovery, and notification results.
  They must call the C policy for strings, warnings, timestamps, versions, and
  basic errors rather than retaining duplicate escaping rules. Clipboard success
  paths are the explicit compatibility exception: their legacy serializer inserts
  path bytes raw, and changing pathological quote/control-byte behavior requires
  a separate public-contract decision.
- `preview/preview_result.{c,h}` owns the typed final-result parser at the Preview
  helper stdout boundary. `src/main.c` and `src/preview/` owns helper execution, stable
  error mapping, and caller-local transfer into the Preview/post-capture model.

### Notification request boundary

- Notification urgency values cross the ABI as fixed 32-bit integers: low `0`,
  normal `1`, and critical `2`. Image construction modes are hint `0` and icon
  `1`. Successful urgency-token spans borrow immutable process-lifetime literals.
- `shaula_notify_request_init` preserves the historical defaults: normal
  urgency, 2500 milliseconds, transient delivery, and absent image/action
  optionals. Summary and body begin as valid empty borrowed spans.
- `shaula_notify_send_args_build` preserves exact argument order:
  `notify-send`, `--app-name=Shaula`, `--urgency`, urgency,
  `--expire-time`, decimal timeout, optional `--transient`, optional image
  arguments, optional `--action=id=label`, summary, and body.
- Hint mode emits `--hint` and
  `string:image-path:file://...`; icon mode emits `-i` and the original borrowed
  image path. Present empty image and action values remain present rather than
  collapsing to absence.
- File-URI escaping is bytewise. `/`, ASCII alphanumerics, `-`, `_`, `.`, and
  `~` remain literal; every other byte becomes uppercase `%XX`. Embedded NUL,
  invalid UTF-8, and arbitrary non-ASCII bytes are observed through explicit
  lengths and are never silently truncated.
- Input spans are borrowed for the synchronous build and argv execution window.
  NULL plus zero length is empty; NULL plus nonzero length is invalid. The C
  module performs no UTF-8 validation, path normalization, filesystem access,
  shell execution, locale classification, or mutable-global-state access.
- Successful send-argument output owns only the decimal timeout, optional image
  hint, and optional action argument as GLib-owned, length-bearing buffers with
  trailing-NUL storage. Literals and request fields remain borrowed. Callers
  clear output through `shaula_notify_send_args_clear`; replacement and repeated
  cleanup are safe.
- `src/main.c` includes `notify/request.h` directly, retains the request bytes
  through process execution, converts only fixed-width statuses and spans, and
  copies a standalone file URI into its existing caller allocation only where its
  public helper API requires owned bytes. No shared Zig notification-policy
  facade remains.
- `src/main.c` and `src/main.c` continue to own actual process execution,
  hint-to-icon fallback decisions, action-output handling, file-manager reveal,
  logging, and public command JSON. This model slice does not absorb those
  command-family responsibilities.

### Public error-taxonomy boundary

- `errors/taxonomy.{c,h}` is the sole owner of public error records, canonical
  ordering, class/action tokens, exit-code mapping, and retry budgets. Callers
  must not duplicate those tables or mappings.
- The maintained callers are the top-level dispatcher, capabilities and
  preflight probes, capture command/guards/lifecycle, clipboard, config,
  directory, doctor, errors, explore, history, notify, preview, settings, and
  setup commands. Each includes the C header directly and performs only
  immediate caller-local slice/span or record conversion.
- `src/main.c` owns only argument validation and the typed compact
  `errors list` result object. It enumerates the C table, obtains shared JSON
  policy through `cli/json.h`, and must not retain a second taxonomy.
- Inputs are borrowed `data + length` spans. A NULL pointer with nonzero length
  is invalid; empty, malformed, unknown, case-changed, whitespace-padded,
  prefixed, suffixed, non-ASCII-substituted, and embedded-NUL values do not match.
- Successful records and text tokens are borrowed immutable process-lifetime
  storage. No caller frees them, and the C module performs no allocation or
  mutable-global-state access.
- Invalid class/action values return an invalid empty span. Full-spec, exit-code,
  and retry-budget lookups use `ERR_UNKNOWN_UNMAPPED` for every nonmatch.

### Runtime environment boundary

- `runtime/env.{c,h}` owns allocation-free environment value parsing: borrowed
  spans, ASCII whitespace trimming, tri-state booleans, exact bounded unsigned
  integers, and first desktop-token extraction.
- Environment value pointers may be `NULL` to represent a missing variable.
  Returned spans are borrowed from caller-supplied storage, require no free
  function, remain valid across another parser call, and are invalidated only
  when the backing environment is mutated or released.
- The C functions have no mutable global state and are thread-safe while callers
  keep each input buffer stable. Malformed booleans return `INVALID`; malformed
  or overflowing unsigned values return the caller-provided default.
- Maintained Zig owners preserve lookup against each caller's
  `std.process.Environ` and perform immediate ABI conversion and integer-width
  bounding at the owning module. No C environment boundary remains in
  maintained code.

### Runtime path boundary

- `runtime/paths.{c,h}` owns ASCII-trimmed override selection, the
  `XDG_RUNTIME_DIR` then `/tmp` fallback order, byte-exact joining, temporary
  capture-path classification, checked allocation sizes, and POSIX parent
  creation.
- Resolution does not validate absolute paths, normalize separators,
  canonicalize `.` or `..`, inspect the filesystem, or interpret bytes through
  the locale. Repeated and trailing separators, relative or root-looking input,
  and non-ASCII bytes are preserved. Relative input spans may contain embedded
  NUL for resolution and classification; filesystem creation rejects embedded
  NUL because POSIX paths cannot represent it.
- Successful resolution returns a GLib-owned length-bearing buffer with a
  trailing NUL. Callers release it through
  `shaula_runtime_owned_path_clear()`. Inputs are borrowed for the synchronous
  call, outputs are independent, and the C implementation has no mutable global
  state.
- Maintained Zig owners perform caller-provided environment lookup, pass spans
  to C, and copy GLib-owned results into the existing caller allocation only where
  their public API requires owned bytes. No C path boundary
  remains in maintained code.
- This boundary owns runtime state and temporary capture artifacts only.
  Configuration, cache, data, history, durable screenshot placement, tool and
  helper lookup, and process execution remain separate responsibilities.

### Runtime tool-lookup boundary

- `runtime/tool_lookup.{c,h}` owns the exact fixed grim candidate order,
  first-existing absolute lookup, colon-delimited PATH splitting, byte-exact
  candidate joining, and generic filesystem existence checks.
- The preserved contract checks existence only and does not require executable
  permission. Non-executable files and directories count as present. Absolute
  lookup skips empty and relative candidates; generic existence checks accept
  relative and absolute paths.
- Missing and empty PATH values return not found. Leading, repeated, and trailing
  empty PATH components are skipped rather than interpreted as the current
  directory. Nonempty components are not trimmed or normalized and are joined
  exactly as `<component>/<tool>`.
- Relative components, whitespace, repeated separators, `.`, `..`, spaces,
  shell metacharacters, non-ASCII bytes, and empty or absolute-looking tool names
  retain byte-level behavior. Embedded NUL cannot cross the POSIX filesystem
  boundary and is treated as inaccessible.
- Successful fixed-candidate results are borrowed from candidate or immutable
  process-lifetime storage. Successful PATH results are GLib-owned,
  length-bearing, trailing-NUL buffers cleared through
  `shaula_runtime_tool_owned_path_clear()`.
- Capture planning, capabilities, and diagnostics retrieve caller-provided PATH
  and invoke the C API directly, preserving borrowed fixed results and copying
  GLib-owned PATH results only for existing allocator-owned APIs.
- The C implementation has no mutable global state, performs checked size
  arithmetic, uses no shell or locale-sensitive classification, and collapses
  generic existence-check failures to false as the former Zig helper did.

### Runtime helper-resolution boundary

- `runtime/helper_resolution.{c,h}` owns the exact precedence: a nonempty
  ASCII-trimmed override, an existing byte-exact sibling path, then an owned bare
  binary name.
- Overrides are returned without existence or executable-permission validation.
  Missing, empty, and whitespace-only overrides continue to sibling lookup.
- The sibling path is joined exactly as `<executable-dir>/<binary-name>` without
  normalization, canonicalization, shell interpretation, or locale-sensitive
  processing. Existing directories and non-executable files count as present.
- When executable-directory discovery is unavailable or the sibling is missing,
  the resolver returns the bare binary name. This is not eager PATH lookup;
  later process spawning owns PATH resolution and failure mapping.
- Relative and absolute overrides, trailing/repeated separators, empty and
  absolute-looking binary names, `.`, `..`, spaces, shell metacharacters, and
  non-ASCII bytes preserve their byte-level behavior. Embedded NUL cannot match
  a sibling POSIX path but remains representable in the length-bearing bare-name
  fallback.
- Successful results are independent GLib-owned, length-bearing buffers with a
  trailing NUL and are released through
  `shaula_runtime_helper_owned_path_clear()`.
- Preview, Overlay, Settings, and portal capture perform caller-provided
  override lookup, executable-directory discovery, ABI conversion, and owned
  result transfer in their owning module. No C helper-resolution boundary or
  duplicate helper policy remains.
- The C implementation has no mutable global state and checks all size additions.
  It does not spawn processes or absorb the similar crop-helper logic in capture
  lifecycle.

### Runtime previous-area boundary

- `runtime/previous_area_store.{c,h}` owns the deterministic
  `x|y|width|height\n` state format, whole-file ASCII trimming, numeric parsing,
  parent creation, synchronous file I/O, and exact portal-backend exclusion.
- Stores use caller-resolved bytewise paths, create parents through the runtime
  path boundary, create/truncate the target with mode `0666` subject to umask,
  write all bytes, and do not fsync or perform atomic replacement. Zero
  dimensions are serialized verbatim because validity is checked on load.
- Loads fail closed for missing, unreadable, allocation-failed, empty, malformed,
  embedded-NUL, and numerically overflowing state. They require exactly four
  fields, signed 32-bit x/y, and unsigned 32-bit nonzero width/height.
- Parsing trims only ASCII space, tab, carriage return, and newline around the
  entire file. Fields are not individually trimmed. Optional signs and internal
  underscores follow Zig `parseInt` behavior, including unsigned negative zero.
- Backend support is false only for the exact byte string `portal-screenshot`.
  No normalization, case folding, shell interpretation, or locale-sensitive
  processing occurs.
- Geometry uses an asserted 16-byte fixed-width C ABI. The implementation has no
  mutable global state and is safe for concurrent calls targeting distinct
  state files.
- Capture lifecycle resolves the caller-provided state path through the C runtime
  path ABI and converts geometry/status values locally. The state format,
  parser, filesystem behavior, and backend policy remain exclusively C-owned.

### Diagnostics and configuration

- `src/main.c` owns installed/runtime discovery.
- `src/config/config.c` and `src/main.c` owns the `shaula config save` setting-flag grammar and
  applies flags to the config draft.
- `src/config/config.c` and `src/main.c` owns command-level config flags, orchestration, and JSON
  envelopes.
- `settings/settings_config.{c,h}` owns the C-facing Settings model,
  integrated defaults, config path resolution, preset mapping, and permissive
  `config show --json` field extraction. `settings/settings_process.{c,h}` owns
  exact Settings helper argv construction and synchronous process execution.
  The obsolete Zig Settings bridge source was deleted during Phase 2 cleanup.

### Preview boundaries

- `preview/preview_paths.{c,h}` owns the helper-side temporary capture-path
  contract and must stay aligned with `runtime/paths.{c,h}` while the Preview
  helper remains a separate C boundary.
- `preview/preview_geometry.{c,h}` owns Preview geometry and color conversion.
- `preview/preview_image_io.{c,h}` and `preview/preview_clipboard.{c,h}` own
  Preview image and clipboard runtime calls. The clipboard C port intentionally
  replaces shell-mediated text publication with exact argv/stdin and suppresses
  child stdout so nested `--json` output cannot escape the helper boundary.
- `preview/preview_notify.{c,h}` owns best-effort Preview notification argv,
  image-hint fallback, timeout normalization, and silent failure behavior.
- `preview/preview_result.{c,h}` owns exact Preview action values/tokens and the
  complete final helper JSON parser. The parser receives a borrowed byte span
  with an explicit length and trims only outer ASCII space, tab, carriage return,
  and newline. It requires one JSON object and rejects malformed or trailing
  input, non-object roots, duplicate decoded keys at any depth, invalid UTF-8,
  unpaired surrogates, and raw embedded NUL. Known fields remain optional;
  missing or wrong-typed fields use defaults, unknown fields are validated then
  ignored, and unknown action strings map to `unknown` for forward compatibility.
  Escaped `saved_path` strings are decoded byte-exactly and may contain embedded
  NUL. A nonempty result is GLib-owned, length-bearing, trailing-NUL storage that
  is released through `shaula_preview_result_clear()`; inputs and action-token
  spans are borrowed. Allocation failure is explicit and outputs remain
  initialized and clearable on every outcome. `src/main.c` and `src/preview/` copies the
  optional path into its existing caller allocation before C cleanup and preserves
  helper exit handling, `ERR_PREVIEW_RESULT_INVALID`, notifications, Preview CLI
  JSON, and post-capture result semantics.
- `preview/preview_tool_defaults.{c,h}` owns per-tool last-used HUD defaults,
  tolerant INI loading, debounced dirty-key persistence, and cross-preview file
  locking. Inspector/widget state remains in `preview_properties_hud.*`.
- Phase 2 strict cleanup removed the obsolete Zig Preview/Settings bridge
  sources and the former compatibility bridge; the maintained build has one C owner for
  each migrated bridge symbol.
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
