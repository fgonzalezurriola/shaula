# Zig-to-C Port Baseline

Date: 2026-07-10  
Branch: `port`  
Authority: `spec/zig-to-c-port.md`

This file records the initial migration baseline. It describes observed behavior
and host conditions; it is not a claim that every host-dependent integration is
currently green.

## Production build ownership

`build.zig` remains the authoritative production build during coexistence. The
installed executable inventory produced under `zig-out/bin` is:

- `shaula`
- `shaula-overlay`
- `shaula-preview`
- `shaula-settings`
- `shaula-crop-image`
- `shaula-portal-screenshot`

The production install also owns:

- `src/preview/icons/hicolor` under `share/icons/hicolor`;
- `integrations/noctalia/shaula` under
  `share/shaula/integrations/noctalia/shaula`.

Meson is currently a non-authoritative migration/test build. It must not install
or replace production binaries until the final cutover gates are met.

## Source inventory

After Phase 2 strict cleanup, all seven Phase 3 production cutovers, and three
Phase 4 pure-model cutovers, the repository contains:

- Zig source/test paths under `src` after Phase 4 pure-model cutovers, with
  obsolete zero-byte placeholders physically deleted;
- 43 C source files under `src`, including the runtime environment, path,
  tool-lookup, helper-resolution, previous-area, capture-session lock,
  process-execution, core capture-mode, Preview result, and error-taxonomy slices
  plus the prior Preview and Settings ports;
- sixteen C unit-test sources and one shell fixture test under `tests/unit`;
- C-owned Preview, Overlay, Settings, crop, portal, runtime primitives, core
  capture-mode, and Preview helper-result parsing behavior;
- Zig-owned CLI, capture lifecycle, configuration, capability, diagnostics, and
  most remaining pure-model modules. Maintained runtime, capture-mode, and
  Preview-service callers use C headers directly. The seven Phase 3 and one core
  capture-mode obsolete Zig paths have been physically deleted, along with
  the later zero-byte `preview_result.zig`, taxonomy/policy, and `cli/json.zig`
  placeholders.

The complete grouped migration inventory is maintained in
`docs/port/migration-matrix.md`.

## Toolchain observed on the baseline host

- Zig: pinned by the existing project to 0.16.0;
- Meson: 1.11.1;
- Ninja: 1.13.2;
- GCC: 16.1.1;
- Clang: 22.1.8;
- GLib/GIO: 2.88.2;
- GTK: 4.22.4;
- GDK Pixbuf: 2.44.7;
- Cairo: 1.18.4;
- Pango/Cairo: 1.58.0;
- GTK4 layer shell: 1.3.0.

These versions describe one development host and are not new minimum-version
claims.

## Automated baseline

`./dev check` on 2026-07-10 passed with no test, Preview document, or
production-build failure. The previously recorded host-dependent `grim`
expectation is no longer a failing gate in the current checkout.

## Contract fixtures

`tests/fixtures/port/errors-list.json` records the timestamp-free output of:

```bash
shaula errors list --json
```

It freezes the current error codes, messages, retryability, classes, actions,
and exit codes. Future C implementations must compare semantically against this
fixture while preserving canonical ordering where required.

Still required for Phase 0:

- command-family success and failure fixtures;
- config round-trip and atomic-save fixtures;
- helper protocol fixtures;
- productive versus degraded performance samples;
- explicit host-dependency classification for the remaining integration tests.

## C parity slices

`src/preview/preview_geometry.c` is the first translated module. The production
Preview helper and Preview document test link this C implementation. The
obsolete Zig geometry source was deleted during Phase 2 strict cleanup.

`tests/unit/preview_geometry_test.c` covers:

- default color and hexadecimal conversion;
- rectangle creation and normalization;
- clamping, expansion, and union;
- empty, containment, and intersection predicates;
- point distance and point-to-segment distance;
- degenerate segments and zero-size rectangles;
- point clamping.

`src/preview/preview_image_io.c` is the second translated module. The production
Preview helper links the C implementation directly. The obsolete Zig image-I/O
source was deleted during Phase 2 strict cleanup.

`tests/unit/preview_image_io_test.c` covers:

- binary file copying, including embedded NUL bytes;
- missing-path and missing-source error behavior;
- case-insensitive `.png` extension detection;
- GLib-owned path allocation with and without an existing extension;
- exact `xdg-open` argv handling for directories containing spaces and shell
  metacharacters, without shell interpretation.

`src/preview/preview_clipboard.c` is the third translated module. The production
Preview helper compiles it directly. The obsolete Zig clipboard source was
removed during Phase 2 strict cleanup. Two intentional contract hardenings are
classified rather than treated as unexplained parity differences: text
publication uses direct `wl-copy` argv/stdin instead of `/bin/sh -c`, and nested
Shaula JSON stdout is suppressed instead of inheriting the Preview helper's
stdout.

`tests/unit/preview_clipboard_test.c` covers:

- null, empty, spaced, multiline, quoted, percent-sign, and boundary-sensitive
  text inputs permitted by the NUL-terminated C string contract;
- exact `wl-copy --type text/plain` argv and exact subprocess stdin bytes;
- shell metacharacters without shell execution;
- text spawn failures, nonzero exits, inherited stderr, and silenced stdout;
- missing PNG paths and deterministic clipboard `GError` values;
- sibling `shaula` resolution before PATH fallback;
- exact public `clipboard copy-image --input <path> --json` argv;
- PNG spawn failures, nonzero exits, captured stderr, and silenced stdout.

`src/preview/preview_notify.c` is the fourth translated module. The production
Preview helper compiles it directly and no longer links an aggregate Zig bridge
object. The obsolete Zig notification source and unlinked aggregate bridge
marker were deleted during Phase 2 strict cleanup.

`tests/unit/preview_notify_test.c` covers:

- NULL required inputs and valid empty summary/body strings;
- NULL and empty image-path normalization;
- default and explicit `--expire-time` values;
- transient versus persistent argv;
- exact summary/body bytes without shell interpretation;
- bytewise file-URI escaping for image hints;
- hint-first execution and icon fallback only when an image is present;
- spawn, nonzero-exit, and signal failures returning `FALSE` silently;
- suppressed child stdout and stderr.

Both `./dev port-check` and `./dev port-check-asan` pass on the baseline host.
The maintained Meson lane now covers runtime environment parsing, runtime path
resolution and parent creation, runtime tool lookup, runtime helper resolution,
previous-area persistence, core capture modes, Preview helper-result parsing,
Preview geometry, image I/O, clipboard, notification, Settings configuration,
Settings argv/process execution, and fixture-driven Noctalia restart readiness
under AddressSanitizer and UndefinedBehaviorSanitizer where applicable.
`.github/workflows/c-port.yml`
runs the C sanitizer lane with GCC and Clang on pushes to `port` and on pull
requests.

`src/settings/settings_config.c` is the fifth translated bridge. The production
`shaula-settings` helper compiles the C implementation directly and no longer
builds or links a Zig settings object. The obsolete Zig Settings bridge source
was deleted during Phase 2 strict cleanup.

`tests/unit/settings_config_test.c` covers:

- exact enum numeric values, LP64 struct ABI offsets, and integrated defaults;
- GLib-owned string allocation, repeated clear safety, and init/clear cycles;
- size and position preset behavior without collapsing custom values;
- config path precedence and whitespace trimming;
- complete, missing, wrong-typed, malformed, repeated, and unknown JSON fields;
- first-match substring behavior, non-decoded escapes, and incomplete objects;
- nullable and custom floating coordinates;
- signed integer prefixes, exact boundaries, and overflow handling;
- empty, Unicode, spaced, and shell-metacharacter-containing strings.

`src/settings/settings_process.c` now owns the exact `config save --json` argv
and synchronous subprocess mapping used by the GTK Settings helper.
`tests/unit/settings_process_test.c` freezes argument ordering, null-string
fallbacks, literal shell metacharacters, stdout/stderr capture, nonzero and
signal outcomes, and spawn-failure exit code 127. The cutover removed the final
active production/test use of the generic Zig C-compat object builder.

All five Phase 2 production callers use C. Repository-wide source, build, test,
documentation, and generated-script searches confirmed that the obsolete Zig
implementations were no longer compiled or referenced by maintained targets.
Those sources, the unlinked Preview bridge marker, and `runtime/c_compat.zig`
were deleted, completing Phase 2 under the strict module-completion definition.

The production helper build and `./dev dev-install --yes` completed successfully
after the Settings cutover.

## Phase 3 runtime environment slice

`src/runtime/env.{c,h}` now owns the environment value operations formerly
implemented in Zig: raw borrowed spans, ASCII whitespace trimming, tri-state
boolean parsing, exact bounded unsigned parsing, and first desktop-token
extraction. The implementation is allocation-free, locale-independent, and has
no mutable global state.

`tests/unit/runtime_env_test.c` freezes missing, empty, whitespace-only,
non-ASCII, borrowed-lifetime, boolean case/default, signed and underscored
unsigned, exact maximum/overflow, and colon/semicolon desktop-token behavior.

The production `shaula` executable and Zig unit-test root compile `env.c`
directly. Maintained Zig owners now preserve their caller-provided
`std.process.Environ` lookup and perform only immediate pointer/span conversion
at the call site. Characterization, C implementation, production cutover, and
caller cleanup are complete; the obsolete Zig path has been physically deleted.

## Phase 3 runtime path slice

`src/runtime/paths.{c,h}` now owns runtime-state path override selection,
`XDG_RUNTIME_DIR` fallback, byte-exact joining, temporary-capture recognition,
and parent-directory creation. Resolution trims only ASCII space, tab, carriage
return, and newline by reusing `runtime/env.c`; it does not canonicalize,
normalize, require absolute paths, or resolve the filesystem. Missing, empty,
and whitespace-only values fall back to `/tmp/shaula/<relative>`.

The resolver returns GLib-owned byte strings with an authoritative length and a
trailing NUL. Callers release them through
`shaula_runtime_owned_path_clear()`. Relative path spans preserve repeated
separators, trailing slashes, `.`, `..`, non-ASCII bytes, and embedded NUL bytes;
filesystem creation rejects embedded NUL because POSIX paths cannot represent
it. All joined-size additions are overflow-checked before allocation.

`tests/unit/runtime_paths_test.c` freezes override and runtime fallback order,
empty and whitespace-only values, absolute-looking and relative inputs,
repeated separators, `.`, `..`, root and parent behavior, non-ASCII and embedded
NUL bytes, exact allocation overflow, temporary-capture matching, recursive
parent creation, and filesystem failures.

The production `shaula` executable and Zig unit-test root compile `paths.c`
directly and link GLib. Existing callers retain caller-supplied environments,
Zig allocator ownership, slice types, and their public `std.Io` signatures while
calling the C ABI at the owning module. Characterization, C implementation,
tests, production cutover, and caller cleanup are complete; the obsolete Zig
path has been physically deleted.

## Phase 3 runtime tool-lookup slice

`src/runtime/tool_lookup.{c,h}` now owns the fixed grim candidate list,
first-existing absolute lookup, PATH splitting and byte-exact candidate joins,
and generic filesystem existence checks. The implementation uses no shell,
normalization, locale-sensitive classification, or mutable global state.

The preserved contract is intentionally existence-only rather than executable
permission checking: non-executable regular files and directories count as
present. Absolute candidate lookup skips relative and empty values, checks
`/usr/bin/grim`, `/bin/grim`, then `/usr/local/bin/grim`, and returns borrowed
storage. PATH lookup distinguishes a missing or empty PATH only internally—both
produce not found—skips every empty component instead of treating it as the
current directory, preserves whitespace, repeated separators, relative
components, `.`, `..`, absolute-looking tool names, spaces, shell
metacharacters, and non-ASCII bytes, and returns the first matching candidate.

`tests/unit/runtime_tool_lookup_test.c` freezes ordering, relative-versus-absolute
candidate behavior, existence-only checks, missing and empty PATH, skipped empty
components, relative components, repeated separators, empty and absolute-looking
tool names, whitespace, shell metacharacters, non-ASCII bytes, embedded-NUL
rejection, size overflow, and repeated GLib-owned cleanup.

The production `shaula` executable and Zig unit-test root compile
`tool_lookup.c` directly and link GLib. Capture planning, capabilities, and
diagnostics call the C ABI directly while preserving caller-supplied PATH,
borrowed fixed candidates, and Zig-owned PATH results. Characterization, C
implementation, tests, production cutover, and caller cleanup are complete; the
obsolete Zig path has been physically deleted.

## Phase 3 runtime helper-resolution slice

`src/runtime/helper_resolution.{c,h}` now owns the helper precedence used by
Preview, Overlay, Settings, and portal capture: a nonempty ASCII-trimmed
explicit override wins without filesystem validation; otherwise an existing
sibling at `<executable-dir>/<binary-name>` wins; otherwise the resolver returns
an owned copy of the bare binary name.

The final step is intentionally not an eager PATH search. The later process
spawn receives the bare name and performs PATH lookup under the process runtime,
matching the former Zig implementation. Sibling checks are existence-only, so a
non-executable regular file or directory counts as present. Joining is
byte-exact and performs no normalization, canonicalization, shell parsing, or
locale-sensitive classification. Override, sibling, and fallback results are
all independently owned.

`tests/unit/runtime_helper_resolution_test.c` freezes missing, empty,
whitespace-only, relative, absolute, shell-metacharacter, and non-ASCII
overrides; non-executable file and directory siblings; missing executable-dir
and sibling fallbacks; trailing and repeated separators; empty and
absolute-looking binary names; embedded-NUL bare-name preservation; checked
size overflow; invalid spans; and repeated GLib-owned cleanup.

The production `shaula` executable and Zig unit-test root compile
`helper_resolution.c` directly. Preview, Overlay, Settings, and portal capture
call the C ABI directly with caller-supplied overrides and executable-directory
spans, copying only the owned argv result. Characterization, C implementation,
tests, production cutover, and caller cleanup are complete; the obsolete Zig
path has been physically deleted.

## Phase 3 runtime previous-area slice

`src/runtime/previous_area_store.{c,h}` now owns previous-area serialization,
parsing, parent creation, synchronous file I/O, and backend support gating. The
persisted format remains exactly one decimal
`x|y|width|height\n` line. Stores create/truncate the target after using the
runtime-path parent contract, write all bytes without shell or temporary files,
and intentionally do not fsync. Zero dimensions are stored verbatim because the
historical validation point is load time.

Loads remain fail closed. Missing, unreadable, allocation-failed, empty,
malformed, embedded-NUL, or numerically overflowing records produce no geometry.
Only ASCII space, tab, carriage return, and newline are trimmed around the whole
file. Fields are not individually trimmed. Parsing preserves Zig `parseInt`
behavior for optional signs and internal underscores, requires exact signed
32-bit x/y and unsigned 32-bit nonzero width/height, and rejects extra fields or
trailing content. Previous-area remains unsupported only for the exact
`portal-screenshot` backend label.

`tests/unit/runtime_previous_area_store_test.c` freezes exact serialization,
truncation, extreme integer values, zero-dimension store/load behavior, relative
paths and recursive parent creation, signs and underscores, malformed and
boundary numeric inputs, missing/unreadable files, embedded NUL data and paths,
size-overflow handling, invalid ABI spans, and exact backend-label matching.

The production `shaula` executable and Zig unit-test root compile
`previous_area_store.c` directly. `capture/lifecycle.zig` now resolves the state
path and converts the 16-byte geometry/status ABI at its owning boundary. Store
failures remain best-effort at the lifecycle call site, while load failures
remain unavailable. Characterization, C implementation, tests, production
cutover, and caller cleanup are complete; the obsolete Zig path has been
physically deleted.

## Phase 3 runtime capture-session lock slice

`src/runtime/capture_session_lock.{c,h}` now owns cross-process capture locking.
It creates parent directories through the runtime-path contract, creates the lock
file exclusively with mode `0666` subject to umask, and writes exactly the
current decimal PID followed by a newline. Lock scope remains selection and
backend capture only; `capture/lifecycle.zig` releases it before post-capture
Preview work.

Existing files are read with a 64-byte limit. The implementation trims only
ASCII space, tab, carriage return, and newline around the whole file and parses a
signed Linux `pid_t` with optional signs and Zig-compatible internal
underscores. Only `kill(pid, 0)` returning `ESRCH` classifies the owner as stale.
A stale file is unlinked and exclusive creation is retried once. Live,
permission-hidden, malformed, oversized, unreadable, or concurrently replaced
locks deterministically report contention. Release remains best-effort and
idempotence remains owned by the capture-lifecycle lock object.

`tests/unit/runtime_capture_session_lock_test.c` freezes exact PID contents,
recursive parent creation, contention, release/reacquire behavior, malformed and
oversized lock handling, current-process detection, signed/underscored stale PID
parsing, stale replacement, invalid spans, embedded-NUL paths, checked size
overflow, and parent-filesystem failures.

The production `shaula` executable and Zig unit-test root compile
`capture_session_lock.c` directly. `capture/lifecycle.zig` now resolves and
retains the caller-owned lock path, maps C status values, and preserves the
existing release/deinit behavior locally. Characterization, C implementation,
tests, production cutover, and caller cleanup are complete; the obsolete Zig
path has been physically deleted.

## Phase 3 runtime process-execution slice

`src/runtime/process_exec.{c,h}` now owns synchronous child execution for all
existing runtime callers. It executes argv directly without a shell, captures
the parent `PATH` before `fork`, uses Zig's exact default path when missing,
skips empty path components, and performs direct `execve` calls without libc's
text-file shell fallback. Replacement environments completely replace the child
environment while executable discovery still uses the captured parent path.

The captured-output path redirects stdin from `/dev/null`, concurrently drains
stdout and stderr with `poll`, and enforces independent exact byte limits. Binary
output, including embedded NUL bytes, remains length-bearing. Every failure
after `fork`, including output overflow or pipe errors, terminates and reaps the
child before returning. Successful output uses GLib ownership at the C boundary
and is copied into the caller's Zig allocator by the facade.

The stdin path writes all binary input through an explicit pipe, ignores child
stdout/stderr, closes stdin before waiting, and contains `SIGPIPE` to the calling
thread when a child closes early. Termination preserves exited, signaled,
stopped, and unknown classifications. Common spawn errors retain the Zig error
names relied on by existing callers, including `FileNotFound`, `AccessDenied`,
`PermissionDenied`, `InvalidExe`, descriptor quotas, system resources, and
`StreamTooLong`.

`tests/unit/runtime_process_exec_test.c` freezes binary stdout/stderr, literal
argv with shell metacharacters, nonzero and signal termination, 256 KiB
simultaneous stdout/stderr drainage, exact per-stream limits, replacement
environments with parent-PATH lookup, skipped empty PATH components, direct
non-shebang rejection, PATH length overflow, missing and non-executable
commands, invalid ABI spans, binary stdin, nonzero stdin-consumer exits, early
stdin closure, and parent-safe broken-pipe handling.

The production `shaula` executable and Zig unit-test root compile
`process_exec.c` directly. Maintained Zig owners now construct C argv and
environment spans locally while preserving their existing output limits,
allocator ownership, termination handling, binary stdin, and command-specific
error mappings. Characterization, C implementation, tests, production cutover,
and all caller migrations are complete. The seven obsolete Zig paths have been
physically deleted, so Phase 3 strict cleanup is complete.

## Phase 4 core capture-mode slice

`src/core/capture_mode.{c,h}` now owns every public capture-mode table: exact CLI
parsing, canonical CLI tokens, runtime-lane mapping, runtime tokens, backend
labels, interactive-selection requirements, and live/frozen region-mode parsing.
The implementation is allocation-free, byte-exact, case-sensitive,
locale-independent, and returns only process-lifetime literal spans.

The canonical model preserves `quick`, `area`, `fullscreen`, `all-screens`,
`focused`, `window`, `previous-area`, and `all-in-one`. `focused` remains a
compatibility alias for the current-output runtime lane; `previous-area` and
`all-in-one` still execute through the area backend lane; only `quick`, `area`,
and `all-in-one` require interactive selection.

`tests/unit/core_capture_mode_test.c` exhaustively freezes enum ABI values, every
CLI/runtime/backend mapping, all interaction flags, region-mode round trips,
case and whitespace rejection, prefix/suffix rejection, non-ASCII and embedded
NUL behavior, invalid spans, invalid enum values, and borrowed-literal lifetime.

The production `shaula` executable and Zig unit-test root compile
`capture_mode.c` directly. Capture command dispatch and grammar, lifecycle and
invocation mapping, configuration parsing/serialization, and overlay selection
include `core/capture_mode.h` directly. Their Zig code performs only immediate
span conversion and fixed-width status/value handling; no caller owns duplicate
token tables or mapping policy. Characterization, C implementation, tests,
production cutover, and caller cleanup are complete. The obsolete facade body
and its Zig tests are removed, and its former path has now been physically
removed.

## Phase 4 Preview result slice

`src/preview/preview_result.{c,h}` now owns the final JSON object emitted by the
native Preview helper and the exact `close`, `copy`, `save`, `discard`, and
`unknown` action tokens. The parser consumes a borrowed byte span with an
explicit length, trims only outer ASCII space, tab, carriage return, and newline,
and requires one complete JSON object.

Compatibility remains deliberately asymmetric. Missing fields and wrong-typed
known fields retain the historical defaults; unknown fields and unknown action
strings are accepted. Empty, whitespace-only, malformed, non-object, duplicate-
key, invalid-UTF-8, unpaired-surrogate, embedded raw-NUL, and trailing-data
payloads are rejected. Escaped strings are decoded, including a valid embedded
NUL in `saved_path`. Allocation failure is returned explicitly rather than
terminating the process.

A nonempty decoded `saved_path` is an independent GLib allocation with an
authoritative length and a trailing NUL. Callers release it through
`shaula_preview_result_clear()`. `preview/service.zig` includes the C header
directly, maps fixed-width action/status values locally, copies the optional path
into its existing Zig allocator, and preserves the established
`ERR_PREVIEW_RESULT_INVALID`, notification, CLI JSON, and post-capture behavior.

`tests/unit/preview_result_test.c` freezes:

- action enum ordinals, exact tokens, and invalid enum handling;
- close, copy, save, discard, and unknown helper payloads;
- missing versus malformed output and non-object roots;
- optional fields, wrong types, unknown nested values, and complete JSON number
  grammar;
- duplicate decoded keys at the root and nested levels;
- escaped field names, Unicode, surrogate pairs, invalid UTF-8, and embedded NUL
  path bytes;
- null/empty path handling, trailing input, invalid strings, raw NUL input,
  invalid spans, owned-output replacement, and repeated clear safety.

The authoritative Zig build compiles the C module for production and the mixed
unit root. Existing Preview service tests retain caller-level action-token and
length-bearing ownership coverage. Repository-wide maintained imports of the
old Zig implementation are zero. `src/preview_result.zig` has been physically
deleted.

## Phase 4 public error-taxonomy and recovery-policy slice

`src/errors/taxonomy.{c,h}` now owns the canonical 28-entry public error table,
including exact ordering, code and message literals, retryability, failure
class, recovery action, exit code, and bounded retry budget. Failure classes and
recovery actions cross Zig/C as asserted 32-bit values. The table and all token
spans borrow immutable process-lifetime literals; the module performs no
allocation and has no mutable global state.

Lookup consumes explicit-length borrowed spans and is byte-exact. It performs no
trimming, case folding, prefix or suffix matching, Unicode normalization, locale
classification, or embedded-NUL truncation. Invalid spans and unknown values do
not match. `shaula_error_taxonomy_spec_for`, exit-code lookup, and retry-budget
lookup collapse every unmapped value to the final `ERR_UNKNOWN_UNMAPPED` record,
exit code 99, and retry budget 0.

The maintained caller inventory is `main.zig`, capabilities and preflight probes,
capture command/guards/lifecycle, clipboard, config, directory, doctor, errors,
explore, history, notify, preview, settings, and setup commands. Each caller
includes `errors/taxonomy.h` directly and keeps only immediate slice/span or
record conversion. `errors/command.zig` remains the owner of timestamped JSON
emission and canonical field ordering; it enumerates the C table rather than
retaining duplicate policy.

`ERR_PREVIEW_RESULT_INVALID` remains an emitted Preview-service error but was not
present in the pre-migration canonical list or fixture. Compatibility preserves
that state: exact lookup rejects it and command exit mapping uses the established
unknown fallback, exit code 99. The architecture-only
`ERR_CAPABILITIES_PROBE_FAILED` token is likewise not added without an observable
runtime contract.

`tests/unit/error_taxonomy_test.c` freezes enum ABI values, every record and its
canonical position, duplicate-code absence, exact lookup, class/action tokens,
retry budgets, exit codes, invalid enum values, unknown and malformed spans,
case/whitespace/prefix/suffix/non-ASCII/embedded-NUL rejection, borrowed pointer
stability, and deterministic consistency with
`tests/fixtures/port/errors-list.json`. The production `shaula errors list
--json` output matches the fixture semantically and its canonical compact error
array byte-for-byte after excluding the timestamp envelope.

The authoritative Zig build compiles `taxonomy.c` into production and the mixed
unit root. Meson now contains eighteen tests under normal and sanitizer lanes.
Repository-wide maintained imports of the old Zig taxonomy and policy are zero.
The obsolete `src/errors/taxonomy.zig`, `src/recovery/policy.zig`,
`src/recovery/policy_test.zig`, and `src/preview_result.zig` paths have been
physically deleted.

## Phase 4 shared JSON envelope and escaping slice

`src/cli/json.{c,h}` now owns the public contract-version literal, exact JSON
byte-string escaping, warning-array serialization, UTC timestamp formatting, and
the complete shared basic-error envelope. `src/ipc/protocol.zig` retains only the
independent IPC version. The authoritative Zig build compiles `json.c` directly;
Meson exposes the same module to `tests/unit/cli_json_test.c`.

The ABI uses borrowed `data + length` spans and a fixed 32-bit status type.
NULL with zero length is a valid empty span; NULL with nonzero length is invalid.
The contract version is borrowed immutable process-lifetime storage. Successful
builders return GLib-owned length-bearing bytes with trailing-NUL storage and
must be released with `shaula_json_owned_bytes_clear`; repeated clear is safe.
Callers that must retain output in Zig copy it into their existing allocator
before clearing the C result.

Escaping is byte-oriented and matches the previous default Zig stringifier.
The nullable-string API preserves the shared distinction between absent `null`
and a present empty string `""`. Quote and backslash are escaped, slash is
unchanged, backspace/form feed/tab/
carriage return/newline use their short escapes, all other control bytes
`0x00..0x1f` use lowercase `\\u00xx`, and every other byte is copied unchanged.
Consequently valid multibyte UTF-8, invalid UTF-8, and arbitrary non-ASCII bytes
are preserved; embedded NUL is observed and emitted as `\\u0000`. No API silently
treats arbitrary input as NUL-terminated text.

The basic-error builder preserves exact field order
`ok, contract_version, command, timestamp, error, warnings` and appends exactly
one newline. `details_json` remains a borrowed raw fragment and is deliberately
not parsed or validated, preserving the historical compatibility boundary.
Command-specific success and rich error serializers remain in their owning Zig
modules for capture/post-capture, config, history, capabilities, doctor, explore,
Preview, clipboard, directory, settings, notify, and errors-list payloads. Those
modules use only caller-local span, status, allocator, nullable-value, and writer
adaptation; they no longer contain a second escaping or warning-array policy.
For strict compatibility, clipboard success payloads retain their preexisting raw
path insertion rather than silently changing pathological quote/control-byte
output in this slice. That command-specific legacy edge is not a second escaping
policy and remains future cleanup work behind an explicit contract decision.

The direct production caller inventory is the top-level dispatcher; capture
command and post-capture JSON; capabilities and preflight; clipboard, config,
directory, doctor, errors, explore, history, notify, Preview, settings, and setup.
`tests/unit/cli_json_test.c` freezes null versus empty strings, empty and long
strings, every control byte, quotes/backslashes/slashes, embedded NUL, valid and
invalid UTF-8, exact output
lengths, checked overflow, invalid spans, repeated cleanup, warning order, empty
warnings, contract-literal lifetime, timestamps, exact basic-error ordering and
newline framing, and unvalidated raw details.

A temporary clean `HEAD` archive was built for differential comparison. After
normalizing only the top-level timestamp, thirteen representative old/new command
outputs were byte-identical: errors-list, root and family usage errors,
capabilities success and warning arrays, preflight details errors, Preview,
history, clipboard, doctor, settings, capture, config, explore, and an adversarial
command containing quote, backslash, newline, tab, and a control byte. Every
migrated output parsed as exactly one JSON object, ended in one newline, and
wrote no stderr. GCC 16.1.1 and Clang 22.1.8 pass all eighteen normal and
ASan/UBSan Meson tests with warnings as errors.

Maintained imports of the old shared Zig implementation are zero. The obsolete
`src/cli/json.zig` path has been physically deleted, along with the prior
Preview-result and error-taxonomy/recovery placeholders.

## Existing unrelated work

The checkout was clean before this shared JSON slice. Preview/UI and unrelated
command-orchestration work remain outside the migration. This slice changed only
the shared JSON policy, direct caller adapters, build/test integration, and the
required ownership documentation; it did not migrate GTK Preview UI, capture
lifecycle, configuration persistence, notification execution, or history storage.
