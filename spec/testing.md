# Shaula Testing Strategy (Task 17)

See [spec/algo.md](algo.md) for the locked engineering decisions and [spec/performance.md](performance.md) for the budgets the tests protect.

## Scope

This document defines the deterministic, machine-first QA strategy for Shaula.
The operational script classification lives in
[`scripts/qa/README.md`](../scripts/qa/README.md); that file is the source of
truth for which wrappers are current gates versus manual/legacy investigation.

Historic QA scripts still exist across three layers:

1. Unit/contracts/mocks (no real compositor required)
2. Integration harness (local deterministic flows)
3. E2E on real Niri session (strictly gated)

The matrix enforces final post-fix contracts for:

- runtime capture backend execution without productive stub success path
- capabilities strict contract between `capabilities list` and `capture <mode>`
- saved default output path `~/Pictures/shaula` with deterministic invalid-path failure
- history retention Top-N 20 newest-first
- overlay v1 base selection and cancellation contract
- shell artifact pre-capture guard with bounded timeout behavior
- optional Noctalia MVP adapter integration without hot-path coupling

## Preflight QA Gate

E2E execution is blocked unless the environment is explicitly ready for Niri/Wayland.

Hard requirements:

- `WAYLAND_DISPLAY` must be set.
- `NIRI_SOCKET` must be set and point to a Unix socket.
- `shaula preflight --json` under `SHAULA_COMPOSITOR=niri` must return `ok=true`.
- `result.wayland=true` and `result.ipc_ready=true` are mandatory before E2E.

Deterministic failure token for non-ready environment:

- `ERR_PREFLIGHT_ENV_NOT_READY`

## Test Matrix

### Current Gate (`./dev check`, C port checks, `git diff --check`, `./dev qa`)

- `./dev check` builds the production mixed-language application and runs the
  maintained Zig/C helper tests.
- `./dev port-check` configures Meson with warnings as errors and runs the
  isolated C migration tests.
- `./dev port-check-asan` runs the same C tests under AddressSanitizer and
  UndefinedBehaviorSanitizer. It disables undefined-symbol rejection for Clang,
  matching the maintained CI compiler matrix.
- The C lane currently contains twenty-two tests. It covers the shared public
  JSON envelope/escaping policy; the public error taxonomy and recovery mapping;
  the core capture-mode model; the compositor environment detector and support/
  overlay policy; capability/runtime backend selection and portal probing;
  focused-output override/process/result resolution; the notification
  request/argv and file-URI model; runtime
  environment
  strings, booleans, bounded unsigned values, and
  desktop tokens; runtime path resolution, ownership, artifact classification,
  and parent creation; runtime tool lookup, helper resolution, previous-area
  persistence, capture-session locking, and process execution; Preview helper-
  result parsing, geometry, image I/O, clipboard, and notifications; Settings
  config parsing/ABI and exact process argv/spawn behavior; plus fixture-driven
  Noctalia restart readiness.
- `git diff --check` rejects whitespace errors.
- `./dev qa` runs the curated non-intrusive contract lane:
  `run-all-tests.sh` -> `run-unit-tests.sh` -> preflight schema, failure
  matrix, and exit-code mapping.

### Public error-taxonomy contract (`tests/unit/error_taxonomy_test.c`)

- Asserted 32-bit failure-class and recovery-action ABI values
- Exact 28-entry public inventory, every field, and canonical fixture ordering
- Duplicate-code absence and exact index/table bounds
- Exact code lookup plus retryability, class, action, exit-code, and bounded
  retry-budget mappings
- Unknown, empty, NULL, malformed, case-changed, whitespace-padded, prefix,
  suffix, non-ASCII, and embedded-NUL rejection
- Invalid spans and invalid class/action enum values without out-of-bounds access
- Canonical `ERR_UNKNOWN_UNMAPPED` fallback with exit code 99 and retry budget 0
- Explicit compatibility checks that emitted-but-unlisted
  `ERR_PREVIEW_RESULT_INVALID` and reserved `ERR_CAPABILITIES_PROBE_FAILED`
  remain unmapped
- Process-lifetime borrowed record/token pointer stability with no allocation
- Deterministic ordered comparison against
  `tests/fixtures/port/errors-list.json`
- Mixed-build command coverage confirming `shaula errors list --json` matches the
  fixture semantically and its compact canonical error array byte-for-byte after
  excluding the timestamp envelope

### Shared JSON contract (`tests/unit/cli_json_test.c`)

- Fixed 32-bit status ABI and borrowed process-lifetime contract-version literal
- Explicit-length spans: NULL plus zero length accepted as empty, NULL plus
  nonzero length rejected, and no hidden NUL-terminated input contract
- Empty strings, plain ASCII, unchanged slash, quote/backslash escaping, named
  control escapes, and every byte `0x00..0x1f` with exact lowercase output
- Nullable strings distinguish absent JSON `null` from a present empty JSON
  string, including invalid presence flags and spans
- Embedded NUL serialized as `\\u0000`; valid multibyte UTF-8, invalid UTF-8,
  non-ASCII bytes, and arbitrary bytes at or above `0x20` preserved unchanged
- Very long strings, exact output lengths, trailing-NUL storage, checked
  allocation-size overflow, invalid spans, and allocation-status propagation
- GLib-owned result replacement, repeated init/clear, and idempotent cleanup
- Warning-array order, exact escaping, empty arrays, and invalid warning spans
- Deterministic `YYYY-MM-DDTHH:MM:SSZ` timestamps, epoch and pre-epoch coverage,
  and out-of-range failure
- Exact basic-error field order, boolean encoding, raw details insertion,
  one-object-plus-one-newline framing, and no partial or doubled object
- Explicit characterization that malformed raw details are not validated, matching
  the previous shared helper contract
- Mixed Zig caller coverage through `capture/command_test.zig` and the full
  `./dev check` command suite
- Temporary clean-`HEAD` differential build: timestamp-only normalization followed
  by byte-for-byte comparison for errors-list, root/capture/config/Preview/history/
  clipboard/doctor/settings/explore usage errors, preflight typed details,
  capabilities warnings, and adversarial quote/backslash/newline/tab/control bytes
- Command-matrix validation that each output is one parseable JSON object, ends in
  exactly one newline, preserves canonical leading field order, and emits no
  stderr
- GCC and Clang normal plus ASan/UBSan Meson lanes with warnings as errors

### Notification request contract (`tests/unit/notify_request_test.c`)

- Fixed 32-bit urgency, image-mode, and status ABI values, including exact low,
  normal, critical, hint, and icon ordinals
- Historical request defaults: normal urgency, 2500 millisecond timeout,
  transient delivery, and absent image/action values
- Exact `notify-send` argv order for default, non-transient, image-hint,
  icon-fallback, action, zero-timeout, and maximum-`u32` timeout requests
- Present empty optionals preserved as image/action values rather than collapsed
  to absence
- File-URI escaping for literal `/`, ASCII alphanumerics, `-`, `_`, `.`, and `~`,
  plus uppercase `%XX` for spaces, punctuation, backslash, embedded NUL, DEL,
  invalid UTF-8, and arbitrary high bytes
- Explicit-length NULL/empty/malformed spans without hidden NUL termination,
  UTF-8 validation, locale dependence, path normalization, filesystem access, or
  shell execution
- Borrowed request/literal argv spans plus GLib-owned decimal timeout, image
  hint, and action buffers with authoritative lengths, trailing-NUL storage,
  replacement safety, and repeated clear behavior
- Invalid presence flags, urgency/image modes, spans, output pointers, and
  checked allocation-size overflow mapped to deterministic status values
- Mixed Zig caller test confirming direct `notify/request.h` use and caller-local
  fixed-width/span/argv adaptation without a shared Zig policy facade
- Clean-`HEAD` command differential checks for copied, error, and
  saved-action-listener paths, including byte-identical captured `notify-send`
  argv and timestamp-normalized public JSON where emitted
- GCC and Clang normal plus ASan/UBSan Meson lanes with warnings as errors

### Core capture-mode contract (`tests/unit/core_capture_mode_test.c`)

- Fixed capture, runtime, and region enum ABI values
- Exact canonical CLI, runtime-lane, backend, and live/frozen region tokens
- Exhaustive public-mode round trips and interaction requirements
- Compatibility aliases: focused to current-output and previous-area/all-in-one
  to the area backend lane
- Case, whitespace, prefix, suffix, non-ASCII, and embedded-NUL rejection
- Invalid spans and enum values without out-of-bounds table access
- Process-lifetime borrowed literal spans and direct maintained-caller C ABI mapping without a Zig policy facade

### Compositor runtime contract (`tests/unit/compositor_runtime_test.c`)

- Fixed 32-bit Niri, Wayland, unsupported, and status ABI values plus stable
  borrowed kind tokens
- Exact detection precedence across `SHAULA_COMPOSITOR`, `NIRI_SOCKET`,
  `XDG_CURRENT_DESKTOP`, `XDG_SESSION_DESKTOP`, `WAYLAND_DISPLAY`, and the
  unsupported fallback
- ASCII-only trimming and desktop-token splitting inherited from
  `runtime/env.{c,h}`, including first-token decisiveness
- Present-empty `NIRI_SOCKET` and `WAYLAND_DISPLAY` behavior; empty and
  whitespace-only explicit/session fallthrough
- Niri canonicalization; complete exact Wayland and wlroots token tables with
  ASCII case-insensitive matching
- Historical case-sensitive lowercase `wayland` substring behavior, including
  the deliberate `foo-wayland-bar` versus `foo-WAYLAND-bar` distinction
- Explicit-length empty, non-ASCII, invalid-UTF-8, embedded-NUL, malformed, and
  invalid-span handling without hidden NUL termination or locale dependence
- Niri/wlroots support and overlay policy plus generic-Wayland portal gating
- Invalid kinds and boolean ABI values returning deterministic invalid outcomes
- Borrowed input labels, immutable process-lifetime canonical labels, no
  allocation/cleanup requirement, and no filesystem/process/global-state access
- Mixed Zig caller coverage through capabilities, preflight, and explore using
  the direct C header, plus focused-output boundary integration without a shared
  Zig compositor facade

### Capability-runtime contract (`tests/unit/capabilities_runtime_test.c`)

- Fixed 32-bit backend-kind and status ABI values plus canonical immutable labels
- Invalid argument handling that resets caller-provided output to an invalid,
  clear non-owning decision
- Exact backend override trimming and matching, unknown-token fallthrough, and
  force-portal precedence
- Niri direct, wlroots grim/portal, generic-Wayland portal gating, unsupported
  compositor, and stub behavior
- Fixed grim candidate lookup without ownership transfer or executable-mode
  reinterpretation
- Caller-supplied portal availability/window overrides, including malformed and
  unavailable-window behavior
- Exact `gdbus call --session --timeout 2 --dest
  org.freedesktop.portal.Desktop --object-path
  /org/freedesktop/portal/desktop --method
  org.freedesktop.DBus.Properties.Get org.freedesktop.portal.Screenshot` argv
  for `version` and `AvailableTargets`
- Independent 2048-byte stdout/stderr limits and advisory absence for spawn,
  lookup, exit, signal, stream-limit, and parse failures
- Last-unsigned parsing and window capability bits `2` and `8`
- Complete capture-mode matrix and exact case-sensitive public/compatibility
  token behavior
- Ordered fallback count/index behavior for every backend kind
- Degraded-backend, overlay-bypass, portal-selection, previous-area, and
  portal-fallback mutation helpers, including malformed decision rejection
- Borrowed environment/compositor labels, immutable backend labels, no result
  allocation or cleanup, and no mutable global state
- Mixed production integration through capture dispatch/lifecycle/guards/backend,
  preflight, capabilities output, and doctor using direct C headers and
  caller-local fixed-layout conversion across independent `@cImport` namespaces

### Focused-output contract (`tests/unit/compositor_focused_output_test.c`)

- Fixed 32-bit status ABI plus initialized, replaceable, and repeat-safe result
  cleanup
- Nonempty ASCII-trimmed `SHAULA_OVERLAY_OUTPUT_NAME` precedence, exact byte
  copying, and no process invocation when the override wins
- Missing, empty, and whitespace-only override fallthrough through the C
  compositor detector
- Exact Niri argv `niri msg -j focused-output`, 8192-byte stdout limit, and
  1024-byte stderr limit
- Exact Sway argv `swaymsg -t get_outputs -r`, 65536-byte stdout limit, and
  1024-byte stderr limit
- Unsupported compositor avoidance with no child process
- Niri required nonempty string `name`, Sway required string `name`, optional
  boolean `focused` defaulting false, first-focused selection, and validation of
  every later array item
- Syntax validation and ignoring of unknown nested fields, while decoded known
  duplicate keys, wrong known types, missing fields, malformed arrays/objects,
  wrong root types, and trailing input invalidate the probe
- JSON string decoding for escaped keys, valid UTF-8, surrogate pairs, invalid
  UTF-8/surrogates, and explicit-length embedded NUL output
- Advisory absence for missing executables, nonzero exits, signals, stream
  overflow, empty output, malformed JSON, and parser/process allocation failure
- Final selected-name allocation as the only propagated out-of-memory result
- GLib-owned output length, trailing-NUL storage, replacement behavior, and
  caller-local copy into existing Zig allocators
- Mixed production integration through Explore, capture lifecycle, and overlay
  selection using the direct C header without a Zig focused-output facade
- Clean-`HEAD` differential matrix comparing timestamp-normalized Explore JSON,
  stderr, and exit status for override, valid/invalid Niri, Unicode/NUL,
  overflow/nonzero, valid/invalid Sway, and unsupported-compositor cases

### Preview helper-result contract (`tests/unit/preview_result_test.c`)

- Fixed 32-bit action ABI values and exact borrowed `close`, `copy`, `save`,
  `discard`, and `unknown` tokens
- Successful helper payloads for every action, exact booleans, nullable path, and
  unknown-action forward compatibility
- Empty/ASCII-whitespace-only output distinguished from malformed JSON and
  non-object roots
- Optional fields, compatibility defaults for missing or wrong-typed known
  fields, and syntax validation of ignored nested values
- Complete JSON number grammar, one-object framing, trailing-data rejection, and
  duplicate decoded-key rejection at root and nested scopes
- Escaped field names and strings, valid UTF-8 and surrogate-pair decoding,
  invalid UTF-8/unpaired-surrogate rejection, and raw versus escaped embedded NUL
- GLib-owned length-bearing `saved_path` bytes with a trailing NUL, explicit
  allocation status, output replacement, invalid-span handling, and repeat-safe
  clear behavior
- Mixed-build `preview/service.zig` tests for the caller-local allocator copy,
  embedded-NUL preservation, exact action serialization, and existing missing/
  save result mapping

### Runtime environment contract (`tests/unit/runtime_env_test.c`)

- Missing, empty, whitespace-only, trimmed, and non-ASCII borrowed values
- Borrowed lifetime across another parser call
- Tri-state, ASCII case-insensitive boolean variants and malformed inputs
- Base-10 unsigned signs, underscores, exact type maxima, overflow, and defaults
- Colon/semicolon desktop tokens, repeated separators, empty tokens, case
  preservation, and exact first-token extraction

### Runtime path contract (`tests/unit/runtime_paths_test.c`)

- Override, `XDG_RUNTIME_DIR`, and `/tmp` fallback ordering with ASCII-only trim
- Missing, empty, whitespace-only, relative, absolute-looking, and non-ASCII
  values
- Byte-exact joins preserving repeated/trailing separators, `.`, `..`, and
  embedded NUL in non-filesystem spans
- Exact size-overflow handling and GLib-owned output clear semantics
- Canonical and substring temporary-capture markers, including negative boundary
  cases
- POSIX parent derivation, root/no-parent no-ops, recursive creation, repeated
  separators, non-canonicalized `..`, embedded-NUL rejection, and filesystem
  failures
- Mixed-build caller integration for caller-supplied environments and Zig
  allocator ownership without a shared facade

### Runtime tool-lookup contract (`tests/unit/runtime_tool_lookup_test.c`)

- Exact fixed grim candidate order and first-existing absolute behavior
- Relative and empty absolute candidates skipped without changing input order
- Existence-only checks for non-executable files and directories
- Generic relative and absolute existence checks, missing paths, embedded NUL,
  and invalid spans
- Missing and empty PATH values plus skipped leading, repeated, and trailing
  empty components
- First-match PATH ordering and byte-exact joins for repeated separators,
  relative components, `.`, `..`, whitespace, spaces, shell metacharacters,
  non-ASCII bytes, empty tools, and absolute-looking tool names
- Checked size overflow, GLib-owned result length/NUL rules, and repeated cleanup
- Mixed-build caller integration for caller-supplied PATH maps, borrowed fixed
  results, and Zig allocator ownership without a shared facade

### Runtime helper-resolution contract (`tests/unit/runtime_helper_resolution_test.c`)

- Explicit override precedence with ASCII-only trimming and no filesystem check
- Missing, empty, and whitespace-only override fallthrough
- Existing sibling selection for non-executable files and directories
- Missing executable-directory and sibling bare-name fallback without eager PATH
  lookup
- Byte-exact joins for trailing/repeated separators, empty and absolute-looking
  binary names, spaces, shell metacharacters, non-ASCII bytes, `.`, and `..`
- Embedded-NUL bare-name preservation when sibling POSIX lookup cannot match
- Checked size overflow, invalid spans, GLib-owned result length/NUL rules, and
  repeated cleanup
- Mixed-build caller integration for caller-supplied environments,
  executable-directory discovery, and Zig allocator ownership

### Runtime previous-area contract (`tests/unit/runtime_previous_area_store_test.c`)

- Exact `x|y|width|height\n` serialization, truncation, full integer ranges, and
  zero-dimension storage
- Relative paths, recursive parent creation, create/truncate behavior, and
  round-trip geometry
- Whole-file ASCII trimming with no per-field trimming
- Optional signs, internal/consecutive underscores, exact i32/u32 limits,
  unsigned negative zero, and nonzero loaded dimensions
- Missing, unreadable, empty, malformed, extra-field, extra-line, embedded-NUL,
  and numeric-overflow records failing closed
- Embedded-NUL and oversized paths, invalid ABI spans, and output initialization
- Exact case-sensitive `portal-screenshot` backend exclusion
- Capture-lifecycle integration for caller-supplied path environments and the
  fixed 16-byte geometry ABI

### Runtime capture-session lock contract (`tests/unit/runtime_capture_session_lock_test.c`)

- Recursive parent creation, exclusive create behavior, and exact decimal PID
  plus newline contents
- Deterministic contention for the current process and malformed, empty,
  whitespace-only, overflowing, or oversized lock records
- ASCII whole-file trimming plus optional signs and Zig-compatible internal PID
  underscores
- Stale-owner classification only on `kill(pid, 0)` returning `ESRCH`, followed
  by one exclusive-create retry
- Best-effort repeated release and successful reacquisition after release
- Invalid spans, checked size overflow, embedded-NUL paths, and parent
  filesystem failures
- Capture-lifecycle integration for caller-supplied lock paths, contention
  mapping, Zig allocator ownership, release-before-Preview, and deinit idempotence

### Runtime process-execution contract (`tests/unit/runtime_process_exec_test.c`)

- Literal argv and shell metacharacters with no shell interpretation
- Binary stdout/stderr, embedded NUL bytes, nonzero exits, signals, and repeated
  output cleanup
- Concurrent 256 KiB stdout/stderr production without pipe deadlock
- Independent exact output limits, first-excess-byte failure, and empty output
  initialization on errors
- Parent-PATH command lookup under replacement environments, exact missing-PATH
  default, and skipped empty PATH components
- Direct `execve` behavior for slash-containing argv0, including rejection of
  executable text without a shebang rather than libc shell fallback
- Missing, non-executable, oversized-PATH, invalid-span, and allocation-overflow
  classifications
- Binary stdin publication, exact file contents, nonzero consumer exits, ignored
  child output, and early-close SIGPIPE containment
- Mixed-build caller integration for allocator-owned output copies, replacement
  environment conversion, common spawn error names, termination reconstruction,
  and binary stdin behavior without a shared facade

### Unit / Contracts / Mocks (`scripts/qa/run-unit-tests.sh`)

- Preflight and capabilities envelope schema checks
- Error taxonomy and exit-code mapping checks
- Deterministic failure matrix without real compositor dependency
- Runtime backend unavailability deterministic failure mapping checks
- Pre-capture guard timeout and warning-token contract checks

### Manual / Legacy Integration (`scripts/qa/run-integration-tests.sh`)

This wrapper remains useful for investigation but is not a default release gate.
It aggregates older task evidence and may include stale runtime assumptions
until individual subchecks are refreshed.

- Direct CLI lifecycle and capture-session lock checks
- Capture core modes (`area`, `fullscreen`) with decoded PNG integrity checks
- Post-capture pipeline (`--save`, `--copy`, partial behavior)
- Capabilities strict contract parity checks (`capabilities.capture.*` vs execution outcomes)
- Default saved-output path checks for `~/Pictures/shaula`
- History Top-N 20 consistency checks
- Overlay base selection checks and deterministic cancel path checks
- Shell artifact guard checks for handshake and bounded settle fallback behavior
- Optional Noctalia adapter checks (action parity, plugin optionality, overhead budget)

### Manual / Legacy E2E Real Niri (`scripts/qa/run-e2e-niri.sh`)

This wrapper requires careful interpretation. It may degrade missing real Niri
preflight in headless environments, and it is not a substitute for targeted
manual checks after overlay/capture/Wayland changes.

- Strict `Preflight QA Gate` before any capture test
- Capture flows: `area`, `fullscreen`, `window`
- Clipboard path behavior checks
- Compositor failure path (`ERR_UNSUPPORTED_COMPOSITOR`)
- Permission/degraded path (`ERR_CLIPBOARD_UNAVAILABLE` under forced unavailable env)
- Runtime/backend state path through `preflight`, `doctor`, and capabilities backend keys
- Strict capability parity and pre-capture guard behavior included in E2E layer report
- Optional Noctalia path checks must not affect capture hot-path pass criteria

## Evidence and Output Contract

- Primary report: `.qa/evidence/task-16-full-regression.json`
- Negative preflight evidence: `.qa/evidence/task-16-full-regression-error.txt`

Report format is stable and machine-checkable:

- `suite`
- `timestamp`
- `pass`
- `layers` (`unit`, `integration`, `e2e_niri`)
- `matrix` with explicit case IDs and pass/fail fields
- `subchecks` for runtime integrity, strict capabilities, default output, history Top-N, overlay base, precondition guard, and optional Noctalia integration

The current `./dev qa` report only contains the curated unit/contract layer.
Older integration/e2e/performance reports are investigation artifacts unless a
release task explicitly refreshes and promotes them.

## Non-Interactive CI Rules

- All scripts use `set -euo pipefail`.
- Expected-failure paths are captured with `set +e` wrappers only where required.
- Scripts emit deterministic `ERR_*` tokens on hard failures.
- No manual acceptance steps are required.
