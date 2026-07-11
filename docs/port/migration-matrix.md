# Zig-to-C Migration Matrix

Status date: 2026-07-11
Branch: `port`

This matrix maps every Zig source/test area to its primary production callers,
current characterization, and planned migration phase. It is an ownership map,
not a substitute for per-module call-graph review before translation.

Status vocabulary:

- **Zig active**: production or tests still compile the Zig module.
- **C active / Zig removed**: production and maintained tests use C; the
  obsolete Zig implementation has been deleted.
- **C active / caller cleanup complete**: production and maintained tests use
  the C ABI directly; no maintained facade imports remain.
- **C active / zero-byte placeholder**: production and maintained tests use C,
  references to the obsolete Zig module are zero, but the current workspace
  interface cannot unlink the tracked path physically.
- **Removed migration support**: an unlinked bridge/helper source was deleted
  after repository-wide reference checks.
- **Test-only Zig**: Zig test root or characterization module.

## Phase 2 — Existing C ABI bridges

| Zig module | Primary callers / boundary | Current characterization | Status |
| --- | --- | --- | --- |
| `runtime/c_compat.zig` | Former allocation/format/status support for the obsolete Zig bridge sources | No maintained production, test, build, or source reference remained | Removed migration support |
| `preview/preview_geometry.zig` | Former geometry export owner for Preview C callers | `tests/unit/preview_geometry_test.c`; Preview document test | C active / Zig removed |
| `preview/preview_image_io.zig` | Former Preview C image copy/path/open-folder exports | `tests/unit/preview_image_io_test.c`; production Preview build/install | C active / Zig removed |
| `preview/preview_clipboard.zig` | Former Preview C PNG/text clipboard export owner | `tests/unit/preview_clipboard_test.c`; production Preview build/install | C active / Zig removed |
| `preview/preview_notify.zig` | Former Preview C notification export owner | `tests/unit/preview_notify_test.c`; production Preview build/install | C active / Zig removed |
| `preview/preview_bridge.zig` | Former aggregate Zig object for Preview C ABI exports | No production or test build reference remained | Removed migration support |
| `settings/settings_config.zig` | Former `native_gtk_settings.c` C ABI model and config-show JSON mapping owner | `tests/unit/settings_config_test.c`; production Settings helper build and symbol inspection | C active / Zig removed |

## Phase 3 — Runtime primitives

| C module / former facade | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `runtime/env.{c,h}` | capability, compositor, capture, runtime lookup, and diagnostics paths | `tests/unit/runtime_env_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/paths.{c,h}` | notification filtering, overlay runtime/state, capture artifacts, previous-area state, and capture locking | `tests/unit/runtime_paths_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/tool_lookup.{c,h}` | capture execution planning, capability probing, and doctor diagnostics | `tests/unit/runtime_tool_lookup_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/helper_resolution.{c,h}` | Preview, Overlay, Settings, and portal helper launch | `tests/unit/runtime_helper_resolution_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/process_exec.{c,h}` | all child-process execution | `tests/unit/runtime_process_exec_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/capture_session_lock.{c,h}` | capture command/lifecycle serialization | `tests/unit/runtime_capture_session_lock_test.c`; production symbol inspection | C active / Zig removed |
| `runtime/previous_area_store.{c,h}` | previous-area capture lifecycle | `tests/unit/runtime_previous_area_store_test.c`; production symbol inspection | C active / Zig removed |

## Phase 4 — Pure models and small command families

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `core/capture_mode.{c,h}` / former capture-mode Zig facade | capture grammar/dispatch, invocation/lifecycle, configuration, and overlay selection | `tests/unit/core_capture_mode_test.c`; direct-caller integration through `./dev check`; production symbol inspection | C active / Zig removed |
| `errors/taxonomy.{c,h}`, `errors/command.zig` / former taxonomy and recovery-policy Zig modules | all public error metadata, action/retry/exit mapping, and `errors list` | `tests/unit/error_taxonomy_test.c`; `tests/fixtures/port/errors-list.json`; mixed command tests | C active / Zig removed |
| `cli/json.{c,h}` / former `cli/json.zig`; `ipc/protocol.zig` IPC-only | shared contract version, byte escaping, warning arrays, timestamps, and basic error envelopes for every command family | `tests/unit/cli_json_test.c`; mixed Zig integration; normalized old/new command differential matrix | C active / Zig removed |
| `preview/preview_result.{c,h}` / former `preview_result.zig` | final Preview helper result parsing in `preview/service.zig`; action tokens consumed by Preview CLI and post-capture JSON | `tests/unit/preview_result_test.c`; direct caller tests in `preview/service.zig`; production mixed-build integration | C active / Zig removed |
| `notify/request.{c,h}` / former `notify/request.zig` | exact notification request defaults, urgency tokens, `notify-send` argv construction, and bytewise file-URI escaping used by `notify.zig` | `tests/unit/notify_request_test.c`; mixed direct-caller test in `notify.zig`; clean-HEAD command differential checks | C active / zero-byte placeholder |
| `notify.zig`, `notify/command.zig` | notification execution, action listener, reveal fallback, and CLI orchestration | post-capture and command-level coverage | Zig active |
| `capture/command_flags.zig`, `capture/command_grammar.zig`, `capture/command_guards.zig`, `capture/warnings.zig` | capture CLI parsing and deterministic usage outcomes | `command_flags_test.zig`, `command_test.zig`, inline grammar tests | Zig active |
| `capture/post_capture_types.zig` | post-capture result and side-effect model | Imported by `test_root.zig` | Zig active |
| `history/store.zig`, `history/command.zig` | history persistence and CLI | Inline/store command coverage | Zig active |
| `directory/command.zig` | directory discovery/open CLI | Command-level behavior fixtures still required | Zig active |
| `clipboard/service.zig`, `clipboard/command.zig` | system clipboard import/export CLI | Command tests and manual clipboard checks | Zig active |
| `selection/selection.zig` | overlay protocol, capture types/JSON/lifecycle | Capture and overlay tests | Zig active |

## Phase 5 — Capability, compositor, diagnostics, and discovery

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `capabilities/runtime.{c,h}` / former `capabilities/runtime.zig`; `capabilities/probe.zig` | backend selection, portal capability probing, capture guards, and `capabilities` CLI serialization | `tests/unit/capabilities_runtime_test.c`; mixed capture/preflight/capabilities/doctor integration | C runtime active / zero-byte placeholder; command Zig active |
| `compositor/runtime.{c,h}` / former `compositor/runtime.zig` | compositor environment precedence, classification, stable kind tokens, and support/overlay policy | `tests/unit/compositor_runtime_test.c`; mixed capabilities/preflight/explore/focused-output integration | C active / zero-byte placeholder |
| `compositor/focused_output.{c,h}` / former `compositor/focused_output.zig` | focused-output override and exact Niri/Sway process/result probing | `tests/unit/compositor_focused_output_test.c`; mixed Explore/capture/overlay integration; clean-HEAD differential matrix | C active / zero-byte placeholder |
| `preflight/probe.zig` | `preflight --json` | Imported by `test_root.zig`; host fixtures required | Zig active |
| `doctor/diagnostics.zig`, `doctor/command.zig` | `doctor --json` | Diagnostics tests imported by `test_root.zig` | Zig active |
| `explore/command.zig` | `explore --json` discovery | Tests imported by `test_root.zig` | Zig active |
| `setup/command.zig` | integration setup/discovery CLI | CLI fixtures and manual Niri checks required | Zig active |

## Phase 6 — Configuration

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `config/config.zig` | typed defaults and public config model | Loader/manager/settings tests | Zig active |
| `config/loader.zig` | strict TOML parsing | Inline tests imported by `test_root.zig` | Zig active |
| `config/save_args.zig` | `config save` flag model | Command tests | Zig active |
| `config/manager.zig` | comment-preserving patch, backup, atomic replace | Inline tests imported by `test_root.zig` | Zig active |
| `config/niri_rule.zig`, `config/niri_keybinds.zig` | managed Niri output and conflict detection | Niri rule tests; integration fixtures required | Zig active |
| `config/command.zig` | config CLI orchestration and JSON | CLI/config round-trip fixtures required | Zig active |

## Phase 7 — Overlay, capture backends, and lifecycle

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `overlay/aspect_store.zig`, `overlay/selection_draft_store.zig`, `overlay/ui_state_store.zig` | persisted overlay state | Inline tests via `test_root.zig` | Zig active |
| `overlay/toolbar_layout.zig` | Overlay toolbar geometry | `overlay/toolbar_layout_test.zig` | Zig active |
| `overlay/helper_protocol.zig`, `overlay/runtime.zig` | helper stdout/stderr/process boundary | Runtime tests; malformed/cancel/timeout fixtures required | Zig active |
| `overlay/capture_session.zig`, `overlay/selection_session.zig` | frozen/live source setup and confirmed selection | Imported by `test_root.zig`; manual overlay checks | Zig active |
| `capture/backends/capture_backend_contract.zig`, `capture/backends/capture_backend_failure.zig` | backend labels, warnings, exit mapping | Backend tests and inline tests | Zig active |
| `capture/backends/capture_backend_output_path.zig`, `capture/backends/capture_backend_png_meta.zig` | artifact path and PNG validation | Backend tests | Zig active |
| `capture/backends/capture_backend_runtime_exec.zig`, `capture/backends/capture_execution_plan.zig` | typed process execution plan | Backend and execution-plan tests | Zig active |
| `capture/backends/portal_screenshot.zig`, `capture/backends/capture_backend.zig` | portal/native backend orchestration | `capture_backend_test.zig` | Zig active |
| `capture/types.zig`, `capture/invocation.zig` | capture request/result and lifecycle mapping | `types_test.zig`, invocation inline tests | Zig active |
| `capture/precondition_guard.zig`, `capture/command_json.zig` | pre-capture guard and public result envelope | Command/lifecycle tests | Zig active |
| `capture/post_capture.zig`, `capture/post_capture_json.zig` | history/copy/preview/save/notify side effects | JSON/types tests and command tests | Zig active |
| `capture/lifecycle.zig`, `capture/command.zig` | end-to-end capture orchestration | Lifecycle and command tests; manual Wayland matrix | Zig active |

## Phase 8 — Remaining executable orchestration

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `preview/service.zig`, `preview/command.zig` | Preview helper launch and CLI | Service tests and manual Preview checks | Zig active |
| `settings/command.zig` | Settings helper launch and CLI | Settings/config integration fixtures | Zig active |
| `main.zig` | top-level command-family dispatch | Full CLI contract matrix required before cutover | Zig active |

## Zig test modules requiring C replacement

| Zig test module | Covers | Replacement direction | Status |
| --- | --- | --- | --- |
| `capture/backends/capture_backend_test.zig` | backend selection/execution/failures | C unit + fault-injection integration tests | Test-only Zig |
| `capture/command_flags_test.zig` | flag parsing | table-driven C unit tests | Test-only Zig |
| `capture/command_test.zig` | command grammar/outcomes | C contract tests against fixtures | Test-only Zig |
| `capture/types_test.zig` | capture types/geometry | C model tests | Test-only Zig |
| `overlay/toolbar_layout_test.zig` | toolbar layout | C geometry/layout unit tests | Test-only Zig |
| former `recovery/policy_test.zig` | error actions, retry budgets, and exit codes | `tests/unit/error_taxonomy_test.c` with fixture comparison | C replacement active / Zig removed |
| `test_root.zig` | aggregate Zig test root | Meson test suites | Test-only Zig |

## Current Phase 2 dependency changes

The authoritative Zig build compiles `src/preview/preview_geometry.c` directly
for both `shaula-preview` and the Preview document test. The obsolete Zig
geometry source has been deleted.

The authoritative Preview helper also compiles
`src/preview/preview_image_io.c`, `src/preview/preview_clipboard.c`, and
`src/preview/preview_notify.c` directly. No aggregate Preview Zig bridge object
is linked anymore. Meson builds all four C modules and their characterization
tests under normal and sanitizer lanes.

The clipboard tests freeze exact PNG CLI argv, sibling-before-PATH resolution,
spawn and nonzero-exit failures, captured PNG stderr, inherited text stderr,
silenced child stdout, and exact text bytes sent to `wl-copy --type text/plain`
without shell interpretation. Direct stdin publication and suppression of nested
Shaula JSON stdout are accepted contract hardenings relative to the former Zig
bridge, not unexplained parity differences.

The notification tests freeze required/empty input behavior, timeout and
transient flags, bytewise file-URI escaping, exact hint and icon argv, hint-first
fallback order, no-image no-fallback behavior, spawn/nonzero/signal failures,
no shell interpretation, and silenced child stdout/stderr.

All four former Preview Zig implementation sources and the unlinked aggregate
bridge marker were deleted after their C slices passed the maintained gates and
repository-wide searches found no maintained references.

`src/settings/settings_config.c` now owns the Settings C ABI model, integrated
defaults, config path resolution, preset mapping, and the existing permissive
`config show --json` field extraction behavior. The authoritative
`shaula-settings` build compiles the C implementation directly and no longer
builds a Zig settings object. `tests/unit/settings_config_test.c` freezes enum
values, LP64 struct offsets, defaults, GLib string ownership, repeated clear
cycles, path precedence, custom values, nullable coordinates, repeated and
malformed fields, substring/escape quirks, integer boundaries, Unicode, spaces,
and literal shell metacharacters. `settings_process.c` and its tests freeze exact
save argv plus spawn/stdout/stderr/exit behavior. The obsolete Zig Settings
bridge source has been deleted.

The Settings cutover removed the last active production/test use of the generic
Zig C-compat build helper. Phase 2 strict cleanup deleted the obsolete Preview
and Settings Zig sources, the unlinked bridge marker, and
`runtime/c_compat.zig`. Production cutover and strict module completion are both
complete for Phase 2.

## Phase 3 runtime environment characterization and cutover

`src/runtime/env.{c,h}` owns the parsing implementation. The authoritative Zig
build compiles `env.c` into both `shaula` and the Zig unit-test root. Every
maintained caller now performs caller-supplied environment lookup and immediate
C-span conversion in its owning module; no shared Zig parsing or facade policy
remains.

Caller inventory:

| Importing module | Functions used | Lifetime/default assumptions |
| --- | --- | --- |
| `capabilities/runtime.{c,h}` | direct `shaula_env_value_trimmed` and `shaula_env_value_flag` use | Backend tokens remain exact after ASCII trim; malformed/missing force-portal defaults false; portal override tri-state behavior is preserved |
| `capture/backends/capture_execution_plan.zig` | `trimmed` | Borrowed helper path is stored in plan argv; caller environment must outlive plan execution |
| `doctor/diagnostics.zig` | `slice` | Raw borrowed values may be stored in the report; derived config paths are allocator-owned |
| `capture/backends/capture_backend.zig` | `trimmed`, `flagEnabled` | Borrowed window ID; missing/malformed flag defaults false |
| `compositor/runtime.{c,h}` | direct `shaula_env_value_trimmed`, `shaula_env_value_slice`, and `shaula_env_first_desktop_token` use | Detection labels borrow environment storage; token case is preserved before C-owned classification |
| `capture/lifecycle.zig` | `flagEnabled`, `trimmed`, `unsignedOrDefault` | Immediate values; malformed flag defaults false and invalid/overflowing settle time defaults to 50 |
| `compositor/focused_output.{c,h}` | direct `shaula_env_value_trimmed` use | Borrowed override is copied into an independent GLib-owned result only when nonempty |
| indirect tests through these modules | all facade functions | Custom `std.process.Environ` maps must be honored; process-global `getenv` would violate the contract |

The C tests freeze these rules:

- raw lookup distinguishes missing from a present empty value;
- trimming removes only ASCII space, tab, carriage return, and newline;
- returned spans borrow caller storage, preserve non-ASCII bytes, and remain
  valid across another environment-parser call while the backing environment is
  unchanged;
- booleans accept `1`, `true`, `yes`, `0`, `false`, and `no`, with ASCII-only
  case folding for words; missing/empty and malformed values remain distinct;
- unsigned parsing is base 10, accepts optional `+`, accepts `-0` but no other
  negative value, preserves Zig's internal-underscore behavior, rejects
  leading/trailing underscores and trailing junk, and applies exact caller type
  maximum/overflow fallback;
- desktop extraction splits on both `:` and `;`, skips empty tokens and repeated
  separators, trims each token, preserves case and non-ASCII bytes, and returns
  the first exact token without substring matching.

Characterization, C implementation, tests, production cutover, and caller
cleanup are complete. Repository-wide facade imports are zero and the obsolete
Zig path has been physically deleted.

## Phase 3 runtime path characterization and cutover

`src/runtime/paths.{c,h}` owns the active implementation. The authoritative Zig
build compiles `paths.c` and its `env.c` trimming dependency into both `shaula`
and the Zig unit-test root and links GLib. Maintained owners now pass their
caller-supplied environment directly to C and copy GLib-owned results only when
their existing Zig API requires allocator-owned bytes.

Direct production caller inventory:

| Importing module | Functions used | Ownership and behavior assumptions |
| --- | --- | --- |
| `notify.zig` | `isRuntimeCaptureArtifact` | Immediate bytewise classification; input is borrowed and never retained |
| `overlay/selection_session.zig` | `resolve` | Resolves the runtime `overlay` directory; returned Zig-allocator-owned bytes are freed after frozen-background path construction |
| `overlay/ui_state_store.zig` | `resolve`, `ensureParent` | Toolbar override or runtime fallback; owned path is freed after load/store and its parent is created before writes |
| `overlay/selection_draft_store.zig` | `resolve`, `ensureParent` | Mode-specific caller logic handles primary/legacy override keys before the generic fallback; owned path is freed after use |
| `overlay/aspect_store.zig` | `resolve`, `ensureParent` | Aspect-file override or runtime fallback; owned path is freed after load/store |
| `capture/backends/capture_backend_output_path.zig` | `captureArtifactDir` | Temporary capture base is allocator-owned and freed before the timestamped output path is returned; durable save-path policy remains outside this module |
| `capture/lifecycle.zig` previous-area adapter | direct path resolution | Previous-area override or runtime fallback; owned path is freed after C-owned load/store, while parent creation occurs inside the C persistence boundary |
| `capture/lifecycle.zig` session-lock adapter | direct path resolution | Lock path ownership moves into `CaptureSessionLock` and is released with its original Zig allocator during deinit |
| mixed-build owner tests | direct C ABI integration | Caller-provided environment maps and allocator ownership remain observable without a test-root facade import |

The frozen C contract is:

- a nonempty override, after ASCII-only trim, wins and is copied exactly;
- otherwise a nonempty ASCII-trimmed runtime directory produces
  `<runtime>/shaula/<relative>`;
- missing, empty, or whitespace-only runtime directories produce
  `/tmp/shaula/<relative>`;
- resolution performs no absolute-path validation, separator normalization,
  canonicalization, `.`/`..` processing, or filesystem lookup;
- repeated and trailing separators, root-looking or relative values, non-ASCII
  bytes, and embedded NUL bytes in the relative span are preserved byte-for-byte;
- joined lengths use exact checked additions and allocation failure or overflow
  returns the out-of-memory status;
- returned paths are GLib-owned, length-bearing, trailing-NUL buffers and are
  released only with `shaula_runtime_owned_path_clear()`; the Zig facade copies
  them before release so existing callers keep their allocator family;
- parent creation rejects embedded NUL, derives POSIX dirname behavior without
  locale classification or canonicalization, creates recursively with mode
  `0755` subject to umask, and treats no-parent and root paths as successful
  no-ops;
- capture-artifact classification is bytewise: the canonical
  `/tmp/shaula/captures/` prefix or any `/shaula/captures/` substring matches,
  while the directory without the trailing separator does not;
- the module has no mutable global state. Environment pointers and span inputs
  are borrowed only for each synchronous call; owned outputs are independent;
- this slice owns runtime state and temporary capture paths only. Configuration,
  cache, data, history, durable screenshot directories, tool lookup, helper
  lookup, process execution, and capture backend policy remain outside it.

Characterization, C implementation, C tests, production cutover, and caller
cleanup are complete. The former test-root import and all production imports are
gone, and the obsolete Zig path has been physically deleted.

## Phase 3 runtime tool-lookup characterization and cutover

`src/runtime/tool_lookup.{c,h}` owns the active implementation. The authoritative
Zig build compiles `tool_lookup.c` into both `shaula` and the Zig unit-test root
and links GLib. Capture planning, capabilities, and diagnostics now call the C
ABI directly while preserving borrowed fixed candidates and allocator-owned PATH
results.

Direct production caller inventory:

| Importing module | Functions used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capture/backends/capture_execution_plan.zig` | `grimPath` | Borrows the process-lifetime fixed candidate and stores it in plan argv; missing candidates map to `error.BackendUnavailable` |
| `capabilities/runtime.{c,h}` | direct `shaula_runtime_tool_grim_path` use | Converts immediate fixed-candidate presence into backend selection; the borrowed path is not retained |
| `doctor/diagnostics.zig` | `findInPathAlloc`, `fileExists` | PATH results are Zig-allocator-owned and released by `ToolStatus.deinit`; generic checks are immediate and treat any existing filesystem object as present |
| mixed-build owner tests | direct C ABI integration | Caller-provided environment maps, borrowed candidates, and allocator ownership remain observable |

The frozen C contract is:

- fixed grim candidates are checked in exact order: `/usr/bin/grim`,
  `/bin/grim`, then `/usr/local/bin/grim`;
- absolute candidate lookup returns the first existing borrowed candidate and
  skips empty or relative candidates;
- checks use existence only, not executable permission, so non-executable files
  and directories count as present;
- generic existence checks accept relative or absolute paths, follow ordinary
  POSIX `access(..., F_OK)` behavior, and collapse inaccessible, invalid,
  embedded-NUL, and internal allocation failures to false;
- missing and empty PATH values both return not found;
- PATH splits only on `:`, skips leading, repeated, and trailing empty
  components, and never treats an empty component as the current directory;
- nonempty components and tool bytes are joined exactly as
  `<component>/<tool>` without trimming, normalization, absolute-tool handling,
  shell interpretation, or locale-sensitive processing;
- relative components, whitespace, repeated separators, `.`, `..`, spaces,
  shell metacharacters, non-ASCII bytes, and empty or absolute-looking tool names
  retain their byte-level behavior;
- PATH lookup returns the first existing GLib-owned candidate with a trailing
  NUL and authoritative length; callers clear it through
  `shaula_runtime_tool_owned_path_clear()` or the Zig facade copies it first;
- size additions are overflow-checked, partially initialized outputs are empty,
  cleanup is repeat-safe, and the module has no mutable global state.

Characterization, C implementation, C tests, production cutover, and caller
cleanup are complete. Repository-wide imports are zero and the obsolete Zig path
has been physically deleted.

## Phase 3 runtime helper-resolution characterization and cutover

`src/runtime/helper_resolution.{c,h}` owns the active resolution policy. The
authoritative Zig build compiles `helper_resolution.c` into both `shaula` and the
Zig unit-test root. Settings, Overlay, Preview, and the portal backend now pass
caller-supplied overrides and executable-directory spans directly to C, then
copy only the owned result required for argv lifetime.

Direct production caller inventory:

| Importing module | Override and binary | Ownership and behavior assumptions |
| --- | --- | --- |
| `settings/command.zig` | `SHAULA_SETTINGS_HELPER_BIN`; `shaula-settings` | Returned Zig-allocator-owned argv element is freed after spawn/wait; spawn failures map to `ERR_SETTINGS_UNAVAILABLE` |
| `overlay/runtime.zig` | `SHAULA_OVERLAY_HELPER_BIN`; `shaula-overlay` | Owned command is freed after `process_exec.runWithEnv`; resolution failures map to overlay unavailability |
| `preview/service.zig` | `SHAULA_PREVIEW_HELPER_BIN`; `shaula-preview` | Owned helper command is freed after preview process execution |
| `capture/backends/portal_screenshot.zig` | `SHAULA_PORTAL_SCREENSHOT_HELPER_BIN`; `shaula-portal-screenshot` | Owned helper path moves temporarily into the capture execution plan and is released by plan cleanup |
| mixed-build owner tests | direct C ABI integration | Caller-provided environment maps, `std.Io`, and allocator ownership remain observable |

The frozen C contract is:

- resolution order is a nonempty ASCII-trimmed explicit override, then an
  existing sibling at `<executable-dir>/<binary-name>`, then an owned copy of
  the bare binary name;
- explicit overrides are not validated for existence or executability and may
  be absolute, relative, whitespace-containing, shell-metacharacter-containing,
  or non-ASCII byte sequences;
- missing, empty, and ASCII-whitespace-only overrides proceed to sibling lookup;
- missing executable-directory discovery skips sibling lookup and returns the
  bare name;
- sibling joining is byte-exact and preserves trailing/repeated separators,
  empty names, absolute-looking names, spaces, shell metacharacters, non-ASCII
  bytes, `.`, and `..` without normalization or shell interpretation;
- sibling checks are existence-only, not executable-permission checks, so
  non-executable files and directories count as present;
- the bare-name result does not prove PATH availability. Later process spawning
  performs PATH lookup, matching the former Zig behavior;
- embedded NUL prevents a sibling POSIX-path match but is preserved in the
  length-bearing bare-name fallback where the API allows it;
- all successful results are independent GLib-owned buffers with authoritative
  lengths and trailing NUL bytes; the Zig facade copies them into the caller's
  allocator before cleanup;
- all size additions are checked, output is empty on errors, cleanup is
  repeat-safe, and the module has no mutable global state.

Characterization, C implementation, C tests, production cutover, and caller
cleanup are complete. Repository-wide imports are zero and the obsolete Zig path
has been physically deleted. The similar crop-helper resolver in
`capture/lifecycle.zig` remains outside this slice and was not refactored.

## Phase 3 runtime previous-area characterization and cutover

`src/runtime/previous_area_store.{c,h}` owns the active previous-area state
format, parsing, parent creation, file I/O, and backend support check. The
authoritative Zig build compiles `previous_area_store.c` into both `shaula` and
the Zig unit-test root. Capture lifecycle now resolves the caller-supplied path
and converts geometry/status values directly at its ownership boundary.

Direct caller inventory:

| Importing module | Functions used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capture/lifecycle.zig` | `store`, `load`, `supportedForBackendLabel` | Successful area captures persist best-effort and suppress every store error; previous-area execution treats absent/malformed state as `ERR_PREVIOUS_AREA_UNAVAILABLE`; portal backend is rejected before loading |
| capture lifecycle tests | direct C ABI integration | Caller-provided environment maps, runtime-path ownership, geometry ABI conversion, and C persistence remain observable |

The frozen C contract is:

- the file format is exactly one decimal `x|y|width|height\n` line;
- stores resolve no environment themselves, create parents through
  `runtime/paths`, open with create/truncate semantics and mode `0666` subject to
  umask, write the complete line, close, and do not fsync or use a temporary
  replacement file;
- stores serialize the full i32/u32 ranges and preserve zero width or height;
  dimension validity is enforced only when loading, matching the former Zig
  implementation;
- loads read the complete file and fail closed for missing, unreadable,
  allocation-failed, empty, malformed, embedded-NUL, or overflowing data;
- only ASCII space, tab, carriage return, and newline are trimmed around the
  whole file. Individual fields are not trimmed;
- exactly four `|`-delimited fields are required. x/y parse as signed 32-bit,
  width/height parse as unsigned 32-bit and must be nonzero;
- decimal parsing preserves Zig `parseInt` behavior for optional plus/minus,
  internal underscores, rejected leading/trailing underscores, and unsigned
  negative zero;
- paths remain bytewise and may be relative. Embedded NUL cannot cross POSIX
  filesystem calls, and checked size arithmetic precedes all path scanning or
  allocation;
- backend support returns false only for the exact byte string
  `portal-screenshot`; case and surrounding bytes are significant;
- geometry uses a fixed 16-byte ABI with asserted offsets, no mutable global
  state is used, and concurrent calls are safe for distinct state files.

Characterization, C implementation, C tests, production cutover, and lifecycle
caller cleanup are complete. Repository-wide imports are zero and the obsolete
Zig path has been physically deleted.

## Phase 3 capture-session lock characterization and cutover

`src/runtime/capture_session_lock.{c,h}` owns exclusive lock creation, PID
serialization, stale-owner classification, one-shot stale replacement, and
best-effort release. The authoritative Zig build compiles
`capture_session_lock.c` into both `shaula` and the Zig unit-test root. Capture
lifecycle now resolves and retains the caller-owned path locally, maps C status
values, and preserves release/deinit idempotence without a shared facade.

Direct caller inventory:

| Importing module | Functions used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capture/lifecycle.zig` | `acquire`, `release`, `deinit` | Contention maps to deterministic retryable `ERR_CAPTURE_IN_PROGRESS`; the lock covers selection/backend capture and is released before post-capture Preview; path memory remains owned until deinit |
| capture lifecycle tests | direct C ABI integration | Caller-provided environment maps, Zig allocator ownership, contention mapping, release-before-Preview, and deinit idempotence remain observable |

The frozen C contract is:

- parent directories are created through `runtime/paths` before acquisition;
- the lock is created with `O_CREAT | O_EXCL`, mode `0666` subject to umask, and
  contains exactly the current decimal PID followed by `\n`;
- an existing lock is read with an exact 64-byte limit. Empty, unreadable,
  malformed, or oversized files remain busy rather than being removed;
- only ASCII space, tab, carriage return, and newline are trimmed around the
  whole file;
- pid parsing preserves optional signs, internal/consecutive underscores,
  rejected leading/trailing underscores, exact signed 32-bit boundaries, and
  decimal-only behavior;
- `kill(pid, 0)` success and `EPERM` both remain busy. Only `ESRCH` is stale;
- stale cleanup unlinks the observed path and retries exclusive creation exactly
  once. A racing replacement that produces `EEXIST` reports busy;
- write failures return a filesystem error and preserve the historical behavior
  that the already-created file is not automatically removed;
- release is best-effort, ignores missing/unlink failures, and performs no owner
  verification, matching the former Zig boundary;
- paths are bytewise and may be relative. Embedded NUL is rejected at the POSIX
  boundary, size arithmetic is checked, and the module has no mutable global
  state.

Characterization, C implementation, C tests, production cutover, and lifecycle
caller cleanup are complete. Repository-wide imports are zero and the obsolete
Zig path has been physically deleted.

## Phase 3 process-execution characterization and cutover

`src/runtime/process_exec.{c,h}` owns direct child-process execution, captured
stdout/stderr, replacement environments, stdin publication, termination
classification, and post-fork cleanup. The authoritative Zig build compiles
`process_exec.c` into both `shaula` and the Zig unit-test root. Maintained callers
now construct C argv/environment spans at their owning boundary and retain their
existing limits, output ownership, termination handling, and command-specific
error mapping.

Direct caller inventory:

| Importing module(s) | Functions used | Ownership and behavior assumptions |
| --- | --- | --- |
| `explore/command.zig`, `notify.zig`, `directory/command.zig` | `run` | Callers own bounded output cleanup and command-specific failure mapping; argv is literal and never shell-interpreted |
| `capture/backends/portal_screenshot.zig`, `capture/backends/capture_backend_runtime_exec.zig`, `capture/lifecycle.zig` | `run` | Backend spawn failures remain distinguishable, especially `FileNotFound`; stdout/stderr limits and backend exit mapping remain caller-owned |
| `compositor/focused_output.{c,h}`, `overlay/selection_session.zig` | `run` | C-owned focused probes degrade locally; direct Zig process callers retain their original buffer ownership |
| `overlay/runtime.zig`, `preview/service.zig` | `runWithEnv` | Helper environments completely replace child variables while executable lookup still uses the parent PATH; helper output and deterministic unavailable mapping remain caller-owned |
| `clipboard/service.zig` | `runWithPipeInput` | Binary PNG bytes are written directly to stdin; child stdout/stderr are ignored and success depends only on termination |
| mixed-build owner tests | direct C ABI integration | Zig allocator ownership, replacement-environment transfer, binary stdin, and termination reconstruction remain observable |

The frozen C contract is:

- argv is executed directly without a shell and may contain arbitrary non-NUL
  bytes, spaces, quotes, glob characters, and shell metacharacters;
- the parent PATH is copied before `fork`. Missing PATH uses Zig's exact
  `/usr/local/bin:/bin/:/usr/bin` default;
- bare-command lookup splits only on `:`, skips all empty components, joins
  `<component>/<argv0>` in a `PATH_MAX` buffer, tracks access-denied candidates,
  and otherwise preserves first terminal exec failure behavior;
- argv0 containing `/` bypasses PATH. All candidates use direct `execve`; no
  libc `ENOEXEC` shell fallback is permitted;
- a replacement environment completely replaces child variables but does not
  affect executable PATH lookup, matching `std.process.run`;
- captured execution redirects stdin from `/dev/null`, drains stdout and stderr
  concurrently with `poll`, accepts exactly each caller-supplied byte limit, and
  returns `StreamTooLong` on the first excess byte;
- captured stdout/stderr are independent binary spans and may contain embedded
  NUL bytes. Successful C buffers are GLib-owned until copied by the facade;
- exec setup errors cross a close-on-exec error pipe. Spawn errors preserve
  missing, access-denied, permission-denied, invalid executable, directory,
  busy, name-length, descriptor quota, filesystem, and system-resource
  distinctions used by Zig callers;
- every post-fork failure terminates and reaps the child before return, so output
  overflow, pipe errors, and allocation failures cannot leak child processes;
- stdin execution writes all binary bytes, closes the pipe before waiting,
  ignores child stdout/stderr, and blocks/consumes a newly generated `SIGPIPE`
  in the calling thread so early child closure cannot terminate Shaula;
- termination preserves exited, signaled, stopped, and unknown variants. The
  module has no mutable global state, though ordinary process environment and
  working-directory mutation remain external synchronization concerns.

Characterization, C implementation, C tests, production cutover, and all caller
migrations are complete. All seven Phase 3 runtime primitives have C-owned
production implementations, repository-wide facade imports are zero, and the
seven obsolete Zig paths have now been physically deleted.

## Phase 4 core capture-mode characterization and cutover

`src/core/capture_mode.{c,h}` owns the user-facing capture-mode model. The
authoritative Zig build compiles `capture_mode.c` into both `shaula` and the Zig
unit-test root. Maintained Zig callers include `core/capture_mode.h` directly,
use its fixed-width ABI values, and perform only immediate caller-local span and
status conversion. The obsolete Zig facade body and its tests have been removed.

Direct caller inventory:

| Importing module(s) | Functions/types used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capture/command.zig`, `capture/command_grammar.zig` | direct CLI and region parsing | Exact case-sensitive C results drive deterministic usage failures; no trimming or prefix matching is allowed |
| `capture/invocation.zig`, `capture/lifecycle.zig` | direct CLI/backend tokens, runtime mapping, region values | Returned token slices are borrowed process-lifetime literals; aliases and area-lane reuse remain C-owned |
| `overlay/selection_session.zig` | fixed-width region values/constants | Live/frozen values pass through existing overlay orchestration without a Zig policy type |
| `config/config.zig`, `config/loader.zig`, `config/manager.zig`, `config/save_args.zig`, `config/command.zig` | direct region parsing and canonical token serialization | Config accepts only exact live/frozen values and serializes borrowed canonical literals without allocation |
| `test_root.zig` | indirect maintained-caller integration | Production Zig callers compile and run against the direct C header while exhaustive table/ABI behavior remains in the C unit test |

The frozen C contract is:

- capture-mode ABI values are fixed from quick `0` through all-in-one `7`;
- exact CLI tokens are `quick`, `area`, `fullscreen`, `all-screens`, `focused`,
  `window`, `previous-area`, and `all-in-one`;
- parsing is bytewise and case-sensitive with no ASCII/Unicode trimming,
  normalization, prefix/suffix acceptance, or embedded-NUL truncation;
- quick, area, previous-area, and all-in-one map to the area runtime lane;
  fullscreen and focused map to current-output; all-screens maps to all-outputs;
  window remains window;
- backend labels preserve the public compatibility vocabulary: fullscreen,
  all-screens, focused, and window remain distinct while quick, area,
  previous-area, and all-in-one use area;
- only quick, area, and all-in-one require interactive selection;
- region modes are exactly live `0` and frozen `1`, with canonical borrowed
  literal tokens;
- invalid enum values return explicit invalid results rather than indexing
  tables, and all returned successful spans borrow immutable process-lifetime
  storage;
- the module performs no allocation, locale classification, I/O, or mutable
  global-state access.

Characterization, C implementation, C tests, production cutover, and caller
cleanup are complete. Repository-wide maintained imports of the former
former capture-mode Zig facade are zero and the obsolete placeholder has been
physically removed; no further cleanup remains for this slice.

## Phase 4 public error-taxonomy characterization and cutover

`src/errors/taxonomy.{c,h}` owns the canonical public error inventory and all
metadata used by maintained callers. The authoritative Zig build compiles
`taxonomy.c` into both `shaula` and the mixed Zig unit-test root. Every maintained
caller includes `errors/taxonomy.h` directly; no shared Zig policy facade remains.
`errors/command.zig` intentionally remains a caller-local JSON serializer until
the separate shared-envelope migration and enumerates the C table directly.

Direct production caller inventory:

| Importing module(s) | C operations used | Ownership and behavior assumptions |
| --- | --- | --- |
| `main.zig`, `capabilities/probe.zig`, `preflight/probe.zig` | exit-code lookup | Exact command errors map through borrowed explicit-length spans; unknown values use exit 99 |
| `capture/command.zig`, `capture/command_guards.zig`, `capture/lifecycle.zig` | exit-code lookup and full-spec fallback lookup | Overlay errors consume borrowed code/message literals immediately; capture JSON ownership remains in Zig |
| `clipboard/command.zig`, `config/command.zig`, `directory/command.zig`, `doctor/command.zig` | exit-code lookup | Caller-provided error strings are not retained by C |
| `explore/command.zig`, `history/command.zig`, `notify/command.zig`, `preview/command.zig`, `settings/command.zig`, `setup/command.zig` | exit-code lookup | Existing command envelopes and messages remain caller-owned |
| `errors/command.zig` | table count/index, class/action tokens, exit-code lookup | C records are borrowed for immediate deterministic JSON emission in canonical order |
| mixed Zig unit root | direct maintained-caller integration | The old recovery-policy test import is removed; C tests own exhaustive policy characterization |

The frozen C contract is:

- the public table contains exactly 28 records in the pre-migration fixture
  order, from `ERR_CLI_USAGE` through final `ERR_UNKNOWN_UNMAPPED`;
- code, message, class token, and action token bytes are exact immutable
  process-lifetime literals and require no cleanup;
- failure-class ordinals are cli `0`, compositor `1`, ipc `2`, backend `3`,
  clipboard `4`, output `5`, and unknown `6`;
- recovery-action ordinals are fail-fast `0`, retry-limited `1`,
  degrade-continue `2`, and degrade-to-portal `3`;
- lookup is explicit-length, byte-exact, case-sensitive, and locale-independent,
  with no trimming, prefix/suffix acceptance, Unicode normalization, or
  embedded-NUL truncation;
- invalid spans, empty strings, malformed bytes, and unknown codes do not match;
  full-spec, exit-code, and retry-budget fallback uses the canonical unknown
  record, exit code 99, and budget 0;
- retry-limited records receive budget 3, degrade-to-portal receives budget 1,
  and all other/non-retryable records receive budget 0;
- invalid class/action enum values return invalid empty spans instead of indexing
  token tables;
- the module allocates nothing and has no mutable global state.

`ERR_PREVIEW_RESULT_INVALID` is currently emitted by Preview service but was not
listed by the old taxonomy or compatibility fixture. It therefore remains
unmapped and preserves the former exit-code-99 behavior. The architecture-only
`ERR_CAPABILITIES_PROBE_FAILED` token is also not synthesized into the public
list without a production caller or fixture contract.

`tests/unit/error_taxonomy_test.c` exhaustively checks every table field and
position, enum ABI, exact lookup, duplicate-code absence, class/action tokens,
retryability, retry budgets, exit codes, invalid enum values, invalid spans,
case/whitespace/prefix/suffix/non-ASCII/embedded-NUL rejection, borrowed pointer
stability, and ordered deterministic fixture consistency. The built `shaula
errors list --json` output matches `tests/fixtures/port/errors-list.json`
semantically and the canonical compact error array byte-for-byte after removing
the timestamp envelope.

Characterization, C implementation, production caller cutover, maintained
import cleanup, and physical deletion of the obsolete
`src/errors/taxonomy.zig`, `src/recovery/policy.zig`, and
`src/recovery/policy_test.zig` paths are complete.

## Phase 4 shared JSON characterization and cutover

`src/cli/json.{c,h}` owns the shared public JSON policy. The authoritative Zig
build compiles `json.c` into production, and every maintained caller includes
`cli/json.h` directly. `src/ipc/protocol.zig` now owns only `ipc_version`; the
public `contract_version` literal is borrowed from C.

Direct caller inventory:

| Importing module(s) | Shared C operations | Command-specific behavior retained in Zig |
| --- | --- | --- |
| `main.zig`, `setup/command.zig` | contract timestamp and complete basic-error envelope | command dispatch and usage decisions |
| `capabilities/probe.zig`, `preflight/probe.zig` | contract version, timestamp, string/warning serialization, basic errors with raw details | capability fields, backend data, compositor details, and IPC version |
| `capture/command_json.zig`, `capture/post_capture.zig`, `capture/post_capture_json.zig` | contract version, timestamp, byte strings, nullable strings, warning arrays | capture fields, selection objects, result duplication, partial/degraded state, and side-effect outcomes |
| `clipboard/command.zig`, `directory/command.zig`, `history/command.zig`, `preview/command.zig`, `settings/command.zig`, `notify/command.zig` | contract version, timestamps, byte strings where historically shared, nullable strings, and basic errors | typed command result payloads and orchestration; clipboard success paths retain legacy raw insertion for exact compatibility |
| `config/command.zig` | contract version, timestamp, byte strings, warning arrays, and basic errors with raw typed details | configuration parsing/persistence, numeric nulls, Niri payloads, and conflicts |
| `doctor/command.zig`, `explore/command.zig`, `errors/command.zig` | contract version, timestamps, strings/warnings, and basic errors | diagnostic/inventory/taxonomy result objects and canonical command-specific field order |

The C ABI is explicit-length and fixed-width. Inputs are borrowed byte spans;
NULL plus zero length is empty, while NULL plus nonzero length is invalid. The
contract literal is immutable process-lifetime storage. Builder results are
GLib-owned length-bearing buffers with trailing-NUL storage and repeat-safe clear.
Caller-local Zig adapters may copy those bytes into an existing allocator but
must not recreate escaping or warning policy.

The nullable-string builder preserves absent `null` versus present empty `""`.
The frozen escaping contract matches the prior default Zig stringifier: quote
and backslash are escaped, slash is not, the five named controls plus backspace
and form feed use short escapes, every other byte `0x00..0x1f` uses lowercase
`\\u00xx`, and all remaining bytes are copied unchanged. This preserves valid
UTF-8, invalid UTF-8, and non-ASCII bytes and converts embedded NUL to
`\\u0000`. The timestamp is `YYYY-MM-DDTHH:MM:SSZ`. The shared basic error has
exact order `ok, contract_version, command, timestamp, error, warnings`, accepts
a borrowed unvalidated raw details fragment for compatibility, and ends in one
newline. Clipboard success-path fields intentionally remain outside the shared
escaping policy because the previous serializer inserted them raw; changing that
pathological quote/control-byte behavior requires a separate contract decision.

`tests/unit/cli_json_test.c` covers null versus empty strings, the complete
byte/control table, empty and long strings, exact lengths, checked overflow,
invalid spans and NULL rules,
valid/invalid UTF-8, embedded NUL, warning order and emptiness, timestamp format,
contract-literal lifetime, repeat-safe init/clear, exact basic-error order, raw
details behavior, and one-object newline framing. The mixed Zig root confirms a
direct caller receives C escaping. A temporary clean `HEAD` build and the
migrated build produced byte-identical output after timestamp-only normalization
for thirteen representative commands, including errors-list, warning arrays,
details errors, usage errors, and adversarial control bytes. GCC and Clang pass
all normal and ASan/UBSan lanes.

Maintained imports of `src/cli/json.zig` are zero. The obsolete path has been
physically deleted.

## Phase 4 Preview result characterization and cutover

`src/preview/preview_result.{c,h}` owns the complete final-result parser for the
native Preview helper and the exact action-token table. The authoritative Zig
build compiles `preview_result.c` into both `shaula` and the Zig unit-test root.
`preview/service.zig` includes the C header directly and retains only caller-
local status/action conversion plus the allocator-family copy required by its
existing public result type.

Direct caller inventory:

| Importing module(s) | Functions/types used | Ownership and behavior assumptions |
| --- | --- | --- |
| `preview/service.zig` | `shaula_preview_result_parse`, init/clear, action constants/tokens | Helper stdout is a borrowed explicit-length span capped by process execution at 4096 bytes; C status values preserve missing versus invalid parsing internally; nonempty GLib-owned `saved_path` bytes are copied exactly into the caller's Zig allocator before C cleanup |
| `preview/command.zig` | `PreviewAction.asString`, `PreviewRunResult` through the service | Existing `close`, `copy`, `save`, `discard`, and `unknown` strings, nullable saved path, booleans, and final preview JSON fields remain unchanged |
| `capture/post_capture.zig`, `capture/post_capture_types.zig`, `capture/post_capture_json.zig` | action and result values through the service | Existing degraded Preview outcome, fallback-notification suppression, and post-capture JSON serialization remain unchanged |
| `test_root.zig` | direct maintained-caller integration | Zig tests exercise empty-output error mapping, save parsing, borrowed action tokens, and exact copying of a decoded embedded-NUL saved path |

The frozen C contract is:

- action ABI values remain close `0`, copy `1`, save `2`, discard `3`, and
  unknown `4`, with exact borrowed process-lifetime tokens;
- input is borrowed and length-bearing, so raw embedded NUL bytes are observed
  rather than silently truncating the document;
- only outer ASCII space, tab, carriage return, and newline are trimmed;
- exactly one complete JSON object is required. Empty/whitespace-only input is a
  distinct missing result, while malformed JSON, non-object roots, trailing data,
  duplicate decoded keys at any nesting level, invalid UTF-8, and unpaired
  Unicode surrogates are invalid;
- `closed`, `action`, `copied`, `saved`, `notified`, and `saved_path` are optional.
  Missing or wrong-typed known fields retain defaults, unknown fields are fully
  syntax-validated and ignored, and unknown action strings map to unknown;
- a nonempty string `saved_path` is decoded exactly, may contain embedded NUL,
  and is returned as an independent GLib-owned buffer with an authoritative
  length and trailing NUL. Null, empty, or wrong-typed paths remain absent;
- initialized outputs are clearable after every outcome, reparsing releases any
  previous owned path, allocation failure returns an explicit status, and the
  implementation has no mutable global state;
- the service preserves existing helper exit handling and maps parser failure to
  the established retryable `ERR_PREVIEW_RESULT_INVALID` outcome.

`tests/unit/preview_result_test.c` exhaustively covers action ABI/tokens, every
helper action, defaults and wrong types, unknown nested values, exact JSON number
grammar, missing/non-object/trailing payloads, duplicate keys including escaped
and embedded-NUL forms, Unicode and surrogate decoding, invalid UTF-8, escaped
and raw NUL behavior, invalid spans, owned-output replacement, and repeat-safe
cleanup. The mixed Zig tests cover the caller-local allocator copy and action
serialization. Characterization, C implementation, tests, production cutover,
and caller cleanup are complete. Repository-wide maintained imports of the old
Zig implementation are zero, and the obsolete `src/preview_result.zig` path has
been physically deleted.

## Phase 4 notification-request characterization and cutover

`src/notify/request.{c,h}` owns the pure notification request policy formerly in
`src/notify/request.zig`: default request values, urgency tokens, exact
`notify-send` argument ordering, decimal timeout formatting, image hint versus
icon construction, optional action formatting, and bytewise file-URI escaping.
Notification execution, fallback decisions, action listening, file-manager
reveal behavior, and the public `notify` command remain in `notify.zig` and
`notify/command.zig`.

Direct caller inventory:

| Importing module | C operations used | Ownership and behavior assumptions |
| --- | --- | --- |
| `notify.zig` | request initialization, send-argument construction and cleanup, file-URI construction and cleanup, urgency/image-mode/status constants | Request text spans borrow Zig storage through synchronous process execution; C-owned timeout, image-hint, and action bytes stay alive until argv consumption completes, then are released with the C clear function |
| mixed Zig unit root | direct import of `notify.zig` | Caller-local span, fixed-width enum/status, and argv-slice conversion compile and execute without a shared Zig facade |

The frozen contract is:

- urgency ABI values are low `0`, normal `1`, and critical `2`; image modes are
  hint `0` and icon `1`, and successful token spans borrow immutable
  process-lifetime literals;
- request initialization produces normal urgency, a 2500 millisecond timeout,
  transient delivery, and absent image/action values, while summary and body are
  valid empty borrowed spans;
- argv order is exactly `notify-send`, `--app-name=Shaula`, `--urgency`, urgency,
  `--expire-time`, decimal timeout, optional `--transient`, optional image
  arguments, optional `--action=id=label`, summary, and body;
- hint mode emits `--hint` plus `string:image-path:file://...`; icon mode emits
  `-i` plus the original borrowed path. Present empty optionals remain present;
- file-URI escaping preserves `/`, ASCII alphanumerics, `-`, `_`, `.`, and `~`;
  every other byte uses uppercase `%XX`, including embedded NUL, invalid UTF-8,
  and arbitrary non-ASCII bytes;
- all inputs are explicit-length borrowed spans. NULL plus zero length is empty;
  NULL plus nonzero length is invalid. The builder does not validate UTF-8,
  normalize paths, inspect the filesystem, execute a shell, or silently truncate
  at embedded NUL;
- the output owns only the decimal timeout, optional image-hint, and optional
  action argument as GLib allocations with authoritative lengths and trailing
  NUL storage. Literal and request-derived argv spans remain borrowed. Output
  replacement and repeated cleanup are safe;
- checked additions and allocation sizes return deterministic overflow or
  out-of-memory statuses, and the module has no mutable global state.

`tests/unit/notify_request_test.c` covers default values, fixed-width ABI values,
borrowed urgency tokens, exact argv order, zero and maximum timeouts, transient
and non-transient requests, hint and icon modes, action formatting, present empty
optionals, every escaping class, embedded NUL and arbitrary bytes, invalid spans,
invalid flags/enums, overflow rejection, output replacement, and repeat-safe
cleanup. A clean `HEAD` build and the migrated build produced byte-identical
captured `notify-send` argv for copied, error, and saved-action-listener command
paths; timestamp-normalized public JSON for copied and error also matched.

Characterization, C implementation, C tests, authoritative Zig build wiring,
Meson normal/sanitizer wiring, direct caller cutover, and maintained import
cleanup are complete. Repository-wide maintained imports of
`src/notify/request.zig` are zero. The current DevSpace interface cannot unlink
tracked files, so that obsolete path is retained only as a zero-byte placeholder
for later physical deletion in a workspace with unlink support.

## Phase 5 compositor-runtime characterization and cutover

`src/compositor/runtime.{c,h}` owns the pure compositor environment detector and
classification policy formerly implemented in `src/compositor/runtime.zig`.
The authoritative Zig build compiles the C module into production and the mixed
unit root. Maintained callers include the C header directly and keep only local
environment lookup, fixed-width enum/span conversion, JSON adaptation, aggregate
backend decisions, or process probing.

Direct caller inventory:

| Importing module | C operations used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capabilities/runtime.{c,h}` | detection, support/overlay policy, wlroots classification, and kind constants | Borrowed labels live in the caller-supplied environment and are embedded in the C-owned fixed-layout runtime decision without allocation |
| `preflight/probe.zig` | detection and borrowed label | Existing unsupported/environment-not-ready errors, exit mapping, JSON fields, and caller-supplied environment remain unchanged |
| `capabilities/probe.zig` | standalone detection fallback | Command JSON and runtime capability serialization remain outside the detector |
| `explore/command.zig` | detection and stable kind tokens | Niri inventory process execution and public inventory JSON remain Zig-owned |
| `compositor/focused_output.{c,h}` | detection and kind constants | The focused-output C boundary reuses borrowed detection immediately before its exact process probes |
| mixed Zig unit root | direct maintained-caller integration | Production callers compile against the C header without a shared Zig facade |

The frozen contract is:

- compositor-kind ordinals are Niri `0`, Wayland `1`, and unsupported `2`; C
  status values are fixed-width and deterministic;
- detection precedence is nonempty ASCII-trimmed `SHAULA_COMPOSITOR`, presence
  of `NIRI_SOCKET`, the first nonempty `XDG_CURRENT_DESKTOP` token split on `:`
  or `;`, nonempty ASCII-trimmed `XDG_SESSION_DESKTOP`, presence of
  `WAYLAND_DISPLAY`, then canonical `unsupported`;
- empty `NIRI_SOCKET` and `WAYLAND_DISPLAY` values still count as present;
  empty/ASCII-whitespace-only explicit and session values fall through;
- the first desktop token is decisive even when unsupported, so later tokens are
  not searched and session/display fallbacks are not consulted;
- Niri comparison is ASCII case-insensitive and canonicalizes its label to the
  process-lifetime literal `niri`; exact known Wayland and wlroots tokens are
  ASCII case-insensitive while every non-Niri label preserves its original bytes
  and case;
- the generic substring compatibility rule remains case-sensitive and recognizes
  only a literal lowercase `wayland` substring; `foo-wayland-bar` is Wayland but
  `foo-WAYLAND-bar` is unsupported;
- explicit-length classification accepts empty spans and observes embedded NUL,
  invalid UTF-8, non-ASCII bytes, prefixes, and suffixes without truncation,
  normalization, or locale-sensitive handling; NULL plus nonzero length is
  invalid;
- support remains true for Niri and wlroots, or for generic Wayland only when the
  portal is available; overlay support remains limited to Niri and wlroots;
- inputs and labels are borrowed, canonical fallback/kind tokens are immutable
  process-lifetime literals, the module allocates nothing, and it performs no
  filesystem, process, shell, or mutable-global-state work.

`tests/unit/compositor_runtime_test.c` covers ABI values, stable kind tokens, the
complete exact-token tables, lowercase substring asymmetry, arbitrary bytes and
embedded NUL, invalid spans/kinds/booleans, complete precedence/fallback order,
present-empty variables, first-token behavior, support/overlay policy, and
borrowed-label behavior. Characterization, C implementation, tests, production
and Meson wiring, direct caller cutover, and maintained import cleanup are
complete. A clean-`HEAD` and migrated command matrix matched normalized JSON,
stderr, and exit codes for fifteen precedence/classification cases. GCC and Clang
normal plus ASan/UBSan lanes passed all twenty tests, and controlled preflight,
capabilities, explore, and doctor checks passed without the host compositor.
Repository-wide maintained imports of `src/compositor/runtime.zig` are zero. The
current DevSpace interface cannot unlink tracked files, so the old path is
retained only as a zero-byte placeholder for later physical deletion.

## Phase 5 focused-output characterization and cutover

`src/compositor/focused_output.{c,h}` owns the advisory focused-output resolver
formerly implemented in `src/compositor/focused_output.zig`. It reuses the C
compositor detector and process-execution boundary, owns the exact Niri/Sway
probe protocols and typed JSON result parsing, and returns an optional
independent name. It does not own backend selection, capture error mapping,
Explore inventory, overlay orchestration, or public JSON.

Direct caller inventory:

| Importing module | C operations used | Ownership and behavior assumptions |
| --- | --- | --- |
| `explore/command.zig` | resolve and clear | Probe failure remains a null focused output; the optional name is copied into the caller allocator before inventory/public JSON construction |
| `capture/lifecycle.zig` | resolve and clear | Resolution runs only for supported fullscreen/focused requests; advisory absence flows into the existing backend-unavailable seam, while result allocation errors retain the prior propagated behavior |
| `overlay/selection_session.zig` | resolve and clear | The optional name scopes frozen-background capture and helper placement; absence preserves compositor-chosen placement and full-output fallback |
| mixed Zig unit root | direct maintained-caller integration | Production callers compile against the direct C header without a Zig policy facade |

The frozen contract is:

- focused-output status values are fixed-width: success `0`, invalid argument
  `1`, and final-result out of memory `2`;
- a nonempty ASCII-trimmed `SHAULA_OVERLAY_OUTPUT_NAME` wins without compositor
  classification or process execution; missing, empty, and whitespace-only
  values fall through;
- Niri runs exactly `niri msg -j focused-output` with stdout limited to 8192
  bytes and stderr to 1024 bytes; Sway runs exactly
  `swaymsg -t get_outputs -r` with stdout limited to 65536 bytes and stderr to
  1024 bytes; every other compositor returns absence without probing;
- spawn and executable lookup errors, stream-limit failures, nonzero exits,
  signals, empty output, malformed JSON, and incomplete typed results are
  best-effort absence and never synthesize a public `ERR_*` failure;
- the Niri result must be one object with a required nonempty string `name`;
  unknown fields are syntax-validated and ignored;
- the Sway result must be one array of objects. Each object requires a string
  `name`; `focused` is an optional boolean defaulting false. The first focused
  nonempty name is selected, but every later element is still fully validated;
- decoded known keys are unique, including escaped spellings. Duplicate unknown
  keys remain accepted for parity with the former typed Zig parser. Wrong known
  types, missing required fields, invalid UTF-8, raw control bytes, malformed
  escapes/surrogates/numbers, invalid root types, and trailing data invalidate
  the whole probe;
- valid JSON strings decode escapes and surrogate pairs exactly. A selected
  escaped `\\u0000` is preserved as an embedded NUL in the explicit-length
  output rather than truncated;
- process and parser allocation failures remain advisory absence, matching the
  former catch-and-fallback boundary. Allocation of the final selected name is
  the only out-of-memory status propagated to callers;
- a successful present name is GLib-owned, has an authoritative byte length and
  trailing-NUL storage, and is released only through
  `shaula_focused_output_result_clear()`. Inputs are borrowed for the synchronous
  call, replacement and repeated cleanup are safe, and there is no mutable
  global state.

`tests/unit/compositor_focused_output_test.c` covers ABI/init/clear behavior,
override precedence and replacement, exact argv, unsupported-compositor
avoidance, Niri/Sway typed parsing, defaults and first-match selection, unknown
nested values, escaped keys, Unicode and embedded NUL, known duplicates,
wrong/missing fields, malformed later array entries, trailing input, nonzero and
signaled children, output overflow, and missing executables. A clean-`HEAD` and
migrated Explore matrix matched timestamp-normalized JSON, stderr, and exit
status for twelve representative cases. Characterization, C implementation,
production and Meson wiring, direct caller cutover, and maintained import/build
cleanup are complete. Meson now exposes twenty C tests plus the shell fixture,
for 21 tests per normal or sanitizer lane. Repository-wide maintained imports of
`src/compositor/focused_output.zig` are zero. The current DevSpace interface
cannot unlink the tracked path, so it remains a zero-byte placeholder pending
physical deletion elsewhere. Final combined validation passed the mixed Zig/C
check, GCC and Clang normal plus ASan/UBSan Meson lanes, all 21 tests in every
lane, the curated QA gate, and whitespace validation.

## Phase 5 capability-runtime characterization and cutover

`src/capabilities/runtime.{c,h}` owns the fixed-layout runtime decision formerly
implemented in `src/capabilities/runtime.zig`. It composes the C compositor,
environment, tool-lookup, process-execution, and capture-mode boundaries without
owning command JSON, capture execution, configuration, or diagnostics rendering.
The authoritative Zig build and Meson both compile the same production source.

Direct caller inventory:

| Importing module | C operations used | Ownership and behavior assumptions |
| --- | --- | --- |
| `capture/command.zig` | aggregate resolve | Caller-supplied environment pointers are borrowed for the synchronous call; the decision is passed to lifecycle through field-by-field C-ABI conversion |
| `capture/lifecycle.zig` | decision helpers, backend label, portal-fallback mutation | Capture ordering and error mapping remain Zig-owned; conversion between independent `@cImport` namespaces copies only fixed fields and borrowed pointers |
| `capture/command_guards.zig` | mode support and backend label | Stub and unsupported-mode JSON/exit behavior remains caller-owned and byte-stable |
| `capture/backends/capture_backend.zig` | backend label and degraded-backend policy | Backend execution receives an already resolved decision and does not probe again |
| `preflight/probe.zig` | aggregate resolve, portal policy, backend label | Existing unsupported and environment-not-ready errors plus JSON field order remain outside C |
| `capabilities/probe.zig` | aggregate resolve, labels, fallbacks, portal policy | Public capability JSON and warning-array serialization remain Zig-owned |
| `doctor/diagnostics.zig` | aggregate resolve and labels | Report allocation and filesystem/tool diagnostics remain Zig-owned |
| `capture/backends/capture_backend_contract.zig` | canonical backend labels | Warning tokens and helper exit-code mapping remain Zig-owned; duplicate backend label literals are removed |
| mixed Zig unit root | direct C-header integration | No maintained import of the retired Zig runtime module remains |

The frozen contract is:

- backend-kind ABI values are Niri direct `0`, grim/wlroots `1`, portal `2`, and
  stub `3`; invalid is `-1`;
- recognized ASCII-trimmed `SHAULA_CAPTURE_BACKEND` values win, followed by a
  valid enabled force-portal flag, Niri direct, wlroots grim/portal selection,
  generic Wayland portal selection, then the historical portal default;
- wlroots selects grim when a fixed candidate exists, otherwise portal when
  available, otherwise grim so execution preserves the existing unavailable
  backend path;
- compositor support and overlay support come from `compositor/runtime.{c,h}`;
  the result embeds its borrowed label and allocates no memory;
- valid portal environment overrides are authoritative. Without one, the module
  runs the exact `gdbus` Properties.Get protocol for `version` and
  `AvailableTargets` with 2048-byte stdout/stderr limits;
- every portal probe failure remains advisory absence. The last unsigned decimal
  value enables window capability for bits `2` or `8`;
- supported non-stub backends expose area, fullscreen, and all-screens; window
  remains false. Unsupported compositors and the stub expose no modes;
- public and compatibility mode tokens remain exact and case-sensitive;
  previous-area is not part of the runtime mode matrix;
- Niri direct, grim, and stub advertise one portal fallback; portal advertises
  none;
- degraded backend, overlay bypass, portal selection, previous-area support, and
  portal-fallback mutation remain deterministic C helpers;
- all inputs and noncanonical compositor labels are borrowed. Backend labels are
  immutable process-lifetime spans. The runtime decision owns no allocation and
  has no cleanup function or mutable global state.

`tests/unit/capabilities_runtime_test.c` covers ABI values and labels, invalid
output reset, Niri and wlroots decisions, generic-Wayland portal gating, exact
override precedence, forced portal and stub matrices, every mode and fallback,
policy helpers and mutation validation, plus a fake `gdbus` executable that
freezes argv ordering and target-bit parsing. Characterization, C implementation,
production and Meson wiring, direct caller cutover, duplicate portal-probe
removal, and maintained import cleanup are complete. Meson exposes twenty-one C
tests plus the shell fixture, for 22 tests per normal or sanitizer lane.
Repository-wide maintained imports and build references to
`src/capabilities/runtime.zig` are zero. The current DevSpace interface cannot
unlink the tracked path, so it remains a zero-byte placeholder pending physical
deletion elsewhere. Final validation passed the mixed Zig/C check, GCC and Clang
normal plus ASan/UBSan Meson lanes, all 22 tests in every lane, the curated QA
gate, and whitespace validation.
