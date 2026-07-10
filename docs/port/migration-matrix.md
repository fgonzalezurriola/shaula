# Zig-to-C Migration Matrix

Status date: 2026-07-10  
Branch: `port`

This matrix maps every Zig source/test area to its primary production callers,
current characterization, and planned migration phase. It is an ownership map,
not a substitute for per-module call-graph review before translation.

Status vocabulary:

- **Zig active**: production or tests still compile the Zig module.
- **C active / Zig removed**: production and maintained tests use C; the
  obsolete Zig implementation has been deleted.
- **C active / temporary Zig facade**: production parsing is C-owned while a
  mechanical Zig lookup/ABI facade remains for existing Zig callers.
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

| Zig module | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `runtime/env.zig` | capability, compositor, capture, helper, runtime lookup, and diagnostics paths | `tests/unit/runtime_env_test.c`; facade test; production symbol inspection | C active / temporary Zig facade |
| `runtime/paths.zig` | config, capture artifacts, history, overlay, Preview paths | Inline tests imported by `test_root.zig` | Zig active |
| `runtime/tool_lookup.zig` | backend/helper/tool diagnostics | Backend and doctor tests | Zig active |
| `runtime/helper_resolution.zig` | Preview, Overlay, Settings and backend helper launch | Command/backend tests | Zig active |
| `runtime/process_exec.zig` | all child-process execution | Inline tests imported by `test_root.zig` | Zig active |
| `runtime/capture_session_lock.zig` | capture command/lifecycle serialization | Capture lifecycle/backend tests | Zig active |
| `runtime/previous_area_store.zig` | previous-area capture lifecycle | Capture lifecycle tests | Zig active |

## Phase 4 — Pure models and small command families

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `core/capture_mode.zig` | capture grammar, flags, capability and lifecycle models | Capture command/type tests | Zig active |
| `errors/taxonomy.zig`, `errors/command.zig` | all public error mapping and `errors list` | `tests/fixtures/port/errors-list.json`; command tests | Zig active |
| `cli/json.zig`, `ipc/protocol.zig` | public JSON envelopes for every command family | Broad command tests; dedicated fixtures being added | Zig active |
| `preview_result.zig` | Preview helper result parsing in `preview/service.zig` | Preview service tests | Zig active |
| `notify/request.zig`, `notify.zig`, `notify/command.zig` | notification model, runtime action, CLI | Inline request tests and post-capture tests | Zig active |
| `capture/command_flags.zig`, `capture/command_grammar.zig`, `capture/command_guards.zig`, `capture/warnings.zig` | capture CLI parsing and deterministic usage outcomes | `command_flags_test.zig`, `command_test.zig`, inline grammar tests | Zig active |
| `capture/post_capture_types.zig` | post-capture result and side-effect model | Imported by `test_root.zig` | Zig active |
| `history/store.zig`, `history/command.zig` | history persistence and CLI | Inline/store command coverage | Zig active |
| `directory/command.zig` | directory discovery/open CLI | Command-level behavior fixtures still required | Zig active |
| `clipboard/service.zig`, `clipboard/command.zig` | system clipboard import/export CLI | Command tests and manual clipboard checks | Zig active |
| `recovery/policy.zig` | error-to-exit/action policy used by main and commands | `recovery/policy_test.zig` | Zig active |
| `selection/selection.zig` | overlay protocol, capture types/JSON/lifecycle | Capture and overlay tests | Zig active |

## Phase 5 — Capability, compositor, diagnostics, and discovery

| Zig module(s) | Primary callers | Current characterization | Status |
| --- | --- | --- | --- |
| `capabilities/runtime.zig`, `capabilities/probe.zig` | backend selection, capture guards, `capabilities` CLI | Runtime module tests and command fixtures | Zig active |
| `compositor/runtime.zig`, `compositor/focused_output.zig` | Niri detection and focused-output capture | Capture/backend tests; live Niri checks | Zig active |
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
| `recovery/policy_test.zig` | error actions and exit codes | C table-driven tests using error fixture | Test-only Zig |
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
build compiles `env.c` into both `shaula` and the Zig unit-test root. The
remaining `runtime/env.zig` file is a temporary mechanical facade: it performs
lookup against the caller-supplied `std.process.Environ`, converts borrowed
sentinel slices to the C span ABI, and supplies the exact unsigned type maximum.
It does not duplicate trimming, boolean, numeric, or token parsing.

Caller inventory:

| Importing module | Functions used | Lifetime/default assumptions |
| --- | --- | --- |
| `capture/backends/portal_screenshot.zig` | `flag` | Immediate tri-state booleans; malformed/missing is null and window capability defaults false |
| `capture/backends/capture_execution_plan.zig` | `trimmed` | Borrowed helper path is stored in plan argv; caller environment must outlive plan execution |
| `doctor/diagnostics.zig` | `slice` | Raw borrowed values may be stored in the report; derived config paths are allocator-owned |
| `capture/backends/capture_backend.zig` | `trimmed`, `flagEnabled` | Borrowed window ID; missing/malformed flag defaults false |
| `compositor/runtime.zig` | `trimmed`, `slice`, `firstDesktopToken` | Detection label may borrow environment storage; token case is preserved before downstream classification |
| `capture/lifecycle.zig` | `flagEnabled`, `trimmed`, `unsignedOrDefault` | Immediate values; malformed flag defaults false and invalid/overflowing settle time defaults to 50 |
| `compositor/focused_output.zig` | `trimmed` | Borrowed override is immediately duplicated with the caller allocator |
| `runtime/tool_lookup.zig` | `slice` | PATH is borrowed only while splitting; successful candidate paths are allocator-owned |
| `runtime/helper_resolution.zig` | `trimmed` | Borrowed override is immediately duplicated with the caller allocator |
| `capabilities/runtime.zig` | `trimmed`, `flagEnabled` | Backend tokens use exact comparisons; malformed/missing force-portal flag defaults false |
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

Characterization, C implementation, tests, and production cutover are complete.
Facade cleanup remains pending and is explicit: migrate the importing Zig
callers, then delete `runtime/env.zig` when repository-wide imports reach zero.
