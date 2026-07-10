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

After Phase 2 strict cleanup and the first Phase 3 slice, the repository
contains:

- 79 Zig source/test files under `src`;
- 34 C source files under `src`, including the runtime environment, Preview, and
  Settings port slices;
- seven C unit-test sources and one shell fixture test under `tests/unit`;
- C-owned Preview, Overlay, Settings, crop, portal, and runtime environment
  surfaces;
- Zig-owned CLI, capture lifecycle, configuration, capability, and diagnostics
  modules, plus a temporary Zig facade over the C environment parser.

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

`./dev check` on 2026-07-10 produced:

- Zig tests: 99 passed, 1 failed;
- Preview document C test: passed;
- production build: passed.

The single failing Zig test is:

```text
capture.backends.capture_backend_test.test.real backend without helper requires grim binary
```

The failure occurs at `src/capture/backends/capture_backend_test.zig:516` and is
host-dependent: the test expects a usable `grim` binary/runtime path. It predates
the C port slice and must be isolated or given an explicit fixture before the
baseline can be called fully green.

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
The maintained Meson lane now covers runtime environment parsing, Preview
geometry, image I/O, clipboard, notification, Settings configuration, Settings
argv/process execution, and fixture-driven Noctalia restart readiness under
AddressSanitizer and UndefinedBehaviorSanitizer where applicable.
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
directly. Remaining Zig callers keep their existing `std.process.Environ`
lookup semantics through a temporary mechanical `runtime/env.zig` facade. That
facade performs only environment lookup, pointer/span ABI conversion, and
integer-width bounds; it contains no independent parsing implementation. Phase 3
characterization, C implementation, and production cutover are complete for
this slice. Facade deletion remains pending until its Zig callers migrate.

## Existing unrelated work

The branch already contained uncommitted Preview/UI changes before the migration
implementation began. Those paths must not be folded accidentally into port
commits. Review and separate them before creating migration commits, as required
by the accepted port specification.
