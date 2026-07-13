# Zig-to-C Port Specification

Status: **Implemented; retained as the historical migration contract**
Completed: **2026-07-13**
Decision record: [`docs/adr/0002-port-zig-core-to-c.md`](../docs/adr/0002-port-zig-core-to-c.md)

This document records the process, target architecture, compatibility rules,
memory-management discipline, verification gates, and completion criteria used
to remove Zig from Shaula and replace the Zig implementation with C. The port is
complete; forward-looking language remains as the historical contract governing
the migration.

The port is an implementation migration. It is not a product rewrite. Existing
public behavior, helper contracts, error semantics, performance budgets, and GTK
interaction behavior remain authoritative unless a separate accepted product
change explicitly modifies them.

Normative terms such as **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are used in
the RFC 2119 sense.

## 1. Decision and objective

Shaula will become a C-only native Linux application. Existing C GTK code is
retained. Zig-owned modules are replaced incrementally with C modules until:

- no production or test source requires Zig;
- no release or development build requires the Zig toolchain;
- no Zig object is linked into any helper;
- the public CLI and helper contracts remain compatible;
- the maintained test, sanitizer, integration, and manual Wayland gates pass.

The port MUST favor behavior preservation over cleanup. Architectural cleanup MAY
happen only when it is necessary to express safe ownership in C or when the
existing implementation cannot be translated without duplicating responsibility.
Unrelated refactors and new product features MUST NOT be mixed into migration
commits.

## 2. Goals

The port has the following goals:

1. Remove Zig from Shaula's source, build, release, and contributor toolchain.
2. Preserve the current process topology and public machine contracts.
3. Preserve the existing GTK Preview, Overlay, and Settings behavior.
4. Establish explicit, reviewable C ownership conventions across the whole
   repository.
5. Retain or improve automated coverage for every migrated Zig module.
6. Keep the capture hot path within the budgets in `spec/performance.md`.
7. Keep each migration step reversible and independently verifiable.
8. End with one build system and one primary implementation language.
9. Avoid hidden fallback behavior, silent error remapping, or contract drift.
10. Make future C changes auditable by ownership, lifetime, and error behavior.

## 3. Non-goals

The port does not include:

- a Rust implementation;
- a Preview UI redesign;
- a new CLI grammar;
- a new configuration format;
- replacement of GTK, GLib, Cairo, Pango, GDK Pixbuf, or layer-shell;
- a daemon or resident service;
- a private socket protocol;
- changes to the `ERR_*` taxonomy unrelated to a demonstrated contract defect;
- new capture modes;
- new editor tools;
- a change from Niri-first/Wayland-first product direction;
- broad compatibility claims not backed by runtime evidence;
- performance claims based only on synthetic or degraded lanes.

## 4. Authoritative behavior

During the port, the following documents remain authoritative:

1. `CONTEXT.md` for current ownership and product behavior.
2. `spec/architecture.md` for CLI, JSON, process, and helper contracts.
3. `spec/configuration.md` for configuration behavior.
4. `spec/performance.md` for latency and resource budgets.
5. `spec/testing.md` for maintained validation expectations.
6. `docs/preview-tools.md` and `docs/preview-ui-contract.md` for Preview behavior.
7. `docs/adr/` for accepted architectural decisions.

When the Zig implementation and a contract document disagree, the discrepancy
MUST be investigated before porting. The C implementation MUST NOT silently copy
an accidental behavior that contradicts an accepted contract, nor silently
"correct" shipped behavior without documenting and testing the change.

## 5. Hard compatibility invariants

The following invariants apply throughout the migration.

### 5.1 Public CLI

The C implementation MUST preserve:

- command-family and command names;
- accepted and rejected flags;
- default values and flag precedence;
- deterministic usage failures;
- exit-code mapping;
- one-object JSON stdout behavior under `--json`;
- public field meanings and required fields;
- canonical `ERR_*` codes;
- retryability classification;
- degraded/partial success semantics;
- stdout versus stderr separation;
- no human-only text on JSON stdout.

JSON object member ordering SHOULD remain canonical where the current contract
specifies it. Tests MUST compare semantic values and contract-required ordering;
they MUST NOT depend on incidental allocator addresses, timestamps, or temporary
paths.

### 5.2 Process topology

The final C implementation MUST preserve the short-lived process topology:

- `shaula` CLI process;
- `shaula-overlay` helper;
- capture backend/helper processes;
- `shaula-preview` helper;
- `shaula-settings` helper;
- `shaula-crop-image` helper;
- `shaula-portal-screenshot` helper.

The port MUST NOT introduce a resident daemon. Helper resolution MUST retain the
order defined in the current runtime contract: explicit environment override,
sibling executable, then `PATH`.

### 5.3 Capture semantics

The C implementation MUST preserve:

- capability rejection before backend execution;
- no automatic retry of capture operations;
- capture-session locking;
- frozen capture using the pre-overlay source with no live fallback;
- live capture settle behavior;
- focused output versus all-output semantics;
- previous-area persistence;
- temporary artifact versus durable output distinctions;
- post-capture copy/save/preview independence;
- degraded non-critical side effects without losing successful capture results.

### 5.4 Configuration

The C implementation MUST preserve:

- config lookup order;
- strict unknown-key and invalid-value rejection;
- integrated defaults for missing config;
- `ERR_CONFIG_UNREADABLE` versus `ERR_CONFIG_INVALID` distinction;
- comment/layout preservation where the current manager promises it;
- backup and atomic replacement behavior;
- Settings saving only through `shaula config save --json`;
- custom values surviving unrelated Settings saves;
- managed Niri block replacement without reloading Niri.

### 5.5 GTK helpers

Existing C GTK behavior MUST remain unchanged while the Zig core is ported.
Migration commits MUST NOT opportunistically rewrite Preview, Overlay, or
Settings interaction behavior unless the changed module is a Zig bridge whose C
replacement can be proven behaviorally equivalent.

### 5.6 Performance

Each migrated hot-path module MUST be measured against the prior implementation.
The port MUST NOT be considered complete if productive capture or overlay timing
regresses beyond the budgets in `spec/performance.md`.

A statistically noisy single run is not sufficient evidence. Use the maintained
benchmark scripts and compare multiple productive runs on the same host/session.

## 6. Target repository architecture

The final repository SHOULD converge toward this layout:

```text
meson.build
meson_options.txt
src/
  main.c
  cli/
  core/
  errors/
  runtime/
  capabilities/
  compositor/
  capture/
  config/
  history/
  clipboard/
  notify/
  doctor/
  preflight/
  explore/
  overlay/
  preview/
  settings/
  setup/
  directory/
tests/
  unit/
  contract/
  integration/
scripts/
  qa/
```

Existing file placement MAY remain during migration. File moves MUST NOT be
combined with semantic translation unless the move is required by the build.

### 6.1 Build system

Meson is the target build system.

Reasons:

- native support for C projects;
- first-class `pkg-config` dependency discovery;
- straightforward executable, test, install, and generated-header definitions;
- good support for GCC and Clang;
- sanitizer and warning build options;
- suitable packaging integration for GTK applications.

During coexistence:

- `build.zig` MAY remain the authoritative release build;
- Meson MUST be introduced early enough to build migrated C modules and tests;
- the same generated or installed artifacts MUST NOT have two competing owners;
- release cutover occurs only after Meson builds every executable and installs
  every icon/integration asset;
- `build.zig`, Zig version checks, and Zig CI setup are removed only in the final
  cleanup phase.

The final Meson build MUST support at least:

```bash
meson setup build
meson compile -C build
meson test -C build
meson install -C build
```

The project SHOULD support a developer configuration with warnings elevated and
sanitizers enabled:

```bash
meson setup build-asan \
  -Db_sanitize=address,undefined \
  -Db_lundef=true \
  -Dwarning_level=3 \
  -Dwerror=true
```

## 7. Compiler and platform policy

The maintained compilers are GCC and Clang on Linux.

Production C MUST target C11 or later. Compiler-specific extensions MAY be used
only when GLib already abstracts them or when guarded by a small compatibility
macro. Core correctness MUST NOT depend on undefined behavior, implementation
integer overflow, unaligned access, or undocumented struct layout.

Every changed C module MUST compile with:

- `-Wall`;
- `-Wextra`;
- `-Wpedantic` where compatible with required GLib macros;
- conversion/sign warnings appropriate to the module;
- no newly introduced warnings.

Warnings MUST NOT be globally suppressed to make translated code compile.
Suppressions MUST be local, documented, and tied to a specific external API
constraint.

## 8. Migration strategy

The port MUST proceed incrementally. A module is not considered migrated merely
because a C version exists; it is migrated only when tests and callers use the C
version and the obsolete Zig implementation has been removed.

### 8.1 Coexistence rule

Temporary Zig-to-C and C-to-Zig seams MAY exist during migration. They MUST:

- use the C ABI;
- use fixed-width or explicitly sized public types;
- document ownership at every pointer boundary;
- avoid exposing Zig allocator-owned memory to long-lived C callers;
- avoid exposing interior C pointers to Zig beyond the immediate call;
- have contract tests;
- be removed once the owning module is migrated.

New permanent cross-language abstraction layers MUST NOT be added.

### 8.2 Translation rule

Port behavior before redesigning it.

For each Zig module:

1. Identify its public callers, owned resources, errors, and tests.
2. Write or strengthen characterization tests if behavior is not explicit.
3. Define the C header and ownership contract.
4. Implement the C module.
5. Run the old and new implementations against the same fixtures where
   practical.
6. Switch one caller or one executable seam.
7. Run unit, contract, integration, sanitizer, and relevant manual checks.
8. Delete the Zig module only after no production/test caller remains.
9. Update `CONTEXT.md` when ownership moves.

Mechanical translation generated by an agent MUST be reviewed for ownership,
integer conversion, error cleanup, nullability, and output-limit behavior before
it is accepted.

## 9. Required migration phases

The phases below define ordering. Work within a phase MAY be split into smaller
commits.

### Phase 0 — Baseline and characterization

Before replacing production behavior:

- record the current executable inventory and install tree;
- capture `shaula errors list --json` and exit-code fixtures;
- capture CLI success/failure fixtures for each command family;
- record current config round-trip fixtures, including comments and custom
  values;
- record helper protocol fixtures for valid, malformed, cancellation, timeout,
  and unavailable cases;
- ensure current performance reports distinguish productive and degraded lanes;
- identify host-dependent tests and isolate their environmental assumptions;
- preserve the current C Preview document tests;
- create a migration test matrix mapping each Zig module to tests and callers.

The current baseline may contain known failures. Every known failure MUST be
recorded with its environmental cause before migration. A port commit MUST NOT
claim to fix or preserve an unexplained failing baseline.

### Phase 1 — Meson and C foundation

Introduce:

- top-level Meson project;
- dependency discovery for GLib, GIO, GTK, GDK Pixbuf, Cairo, Pango, and
  gtk4-layer-shell as needed;
- common warning configuration;
- test executables;
- sanitizer build lane;
- install destinations matching the existing release archive;
- generated version/config headers if needed.

This phase MUST NOT switch production runtime behavior.

### Phase 2 — Existing Zig C-ABI bridges

Port the narrow bridge modules first:

- `runtime/c_compat.zig`;
- `preview/preview_geometry.zig`;
- `preview/preview_image_io.zig`;
- `preview/preview_clipboard.zig`;
- `preview/preview_notify.zig`;
- `settings/settings_config.zig` only after its JSON/config ownership contract is
  fully characterized.

These modules already have C callers and provide the lowest-risk way to validate
memory conventions, Meson linking, and mixed-build operation.

The geometry port MUST have exhaustive parity tests for normalization, clamp,
union, containment, intersection, distance, and degenerate values.

### Phase 3 — Runtime primitives

Port the low-level runtime modules before command orchestration:

- environment access;
- runtime paths;
- tool lookup;
- helper resolution;
- process execution;
- capture-session lock;
- previous-area store.

`process_exec` is a critical seam. Its C replacement MUST preserve:

- argv without shell interpretation;
- explicit environment maps;
- stdin ownership;
- bounded stdout/stderr capture;
- exact exit/signal classification;
- no deadlock when both output streams fill;
- deterministic cleanup on spawn/read/wait failure;
- no leaked child processes;
- no truncation without an explicit outcome.

### Phase 4 — Pure models and small command families

Port strongly typed, mostly pure modules next:

- capture mode model;
- error taxonomy and exit mapping;
- JSON envelope writer;
- preview result parser;
- notify request model;
- command grammar/flag models;
- history store;
- directory commands;
- clipboard command orchestration;
- recovery policy.

The error taxonomy MUST remain centralized. Public error strings MUST NOT be
copied into arbitrary callers.

The JSON writer MUST own escaping and output framing. Callers MUST NOT construct
JSON through ad-hoc `printf` fragments.

### Phase 5 — Capability, compositor, diagnostics, and discovery

Port:

- compositor detection;
- focused-output resolution;
- capability decisions;
- preflight probes;
- doctor diagnostics;
- explore command;
- setup and integration discovery.

Backend selection MUST remain typed. Callers MUST NOT compare public backend
strings to make runtime decisions.

### Phase 6 — Configuration

Port configuration as an isolated milestone:

- typed defaults;
- strict TOML loader;
- save-argument grammar;
- comment-preserving manager;
- Niri rule generation;
- config command JSON output.

Configuration is not complete until the C implementation passes differential
fixtures covering:

- missing file;
- unreadable file;
- invalid syntax;
- unknown section;
- unknown key;
- invalid enum/value;
- complete valid config;
- comments and custom formatting;
- unrelated save preserving custom values;
- backup creation;
- atomic replacement failure;
- generated Niri block replacement.

A TOML library MAY be introduced only after packaging impact is reviewed. If a
third-party parser is used, strict unknown-key validation remains Shaula's
responsibility.

### Phase 7 — Capture backends and lifecycle

Port:

- execution plan;
- output path resolution;
- PNG metadata handling;
- backend helper contracts;
- portal backend orchestration;
- invocation mapping;
- precondition guard;
- post-capture side effects;
- capture lifecycle;
- capture JSON response.

The capture lifecycle cutover MUST use the existing integration and failure
matrix. It MUST include fault injection for:

- unsupported mode;
- missing helper;
- malformed helper output;
- nonzero helper exit;
- timeout;
- output path failure;
- lock contention;
- previous area unavailable;
- clipboard unavailable;
- history unavailable;
- Preview unavailable;
- notification failure;
- frozen source missing;
- live settle behavior.

No capture command MAY be retried automatically.

### Phase 8 — Command orchestration and main executable

Port remaining command dispatch and `src/main.zig` last.

Before switching the installed `shaula` executable, the C CLI MUST pass the full
contract matrix against the same fixtures as the Zig CLI. The migration SHOULD
support side-by-side binaries such as `shaula-zig` and `shaula-c` during parity
testing, but release packages MUST expose only the canonical executable after
cutover.

### Phase 9 — Final build and toolchain removal

Only after every production and test caller uses C:

- make Meson the only maintained build;
- remove `build.zig`;
- remove remaining `.zig` sources;
- remove Zig version pins and CI installation;
- replace Zig-specific QA commands;
- update source and binary packaging metadata;
- verify release archives on supported architectures;
- update README and contributor instructions;
- remove temporary compatibility headers and bridge symbols;
- run repository-wide searches for stale Zig references.

## 10. C memory-management specification

This section is normative for all new and migrated C code.

### 10.1 Ownership vocabulary

Every pointer crossing a module interface MUST have one of these ownership
classes:

- **borrowed**: valid only for the documented call/scope; caller does not free;
- **owned**: caller is responsible for exactly one matching release;
- **transferred**: ownership moves from caller to callee;
- **shared**: lifetime is managed through explicit reference counting;
- **interior**: pointer aliases storage owned by another object and MUST NOT
  outlive or be freed independently.

Public headers MUST document ownership when it is not unambiguous from the naming
rules below.

### 10.2 Naming rules

Use these suffixes consistently:

- `_new`: returns a new owned object;
- `_copy` or `_dup`: returns a deep owned copy;
- `_ref`: acquires one shared reference;
- `_unref`: releases one shared reference;
- `_free`: releases one owned allocation/object;
- `_clear`: releases owned fields but not the containing storage;
- `_init`: initializes caller-provided storage;
- `_take`: callee takes ownership of an argument;
- `_steal`: transfers ownership out and clears the source;
- `_peek`: returns a borrowed/interior pointer with no new reference;
- `_get`: borrowed by default unless the header explicitly says otherwise;
- `_set`: copies/refs input by default;
- `_set_take`: takes ownership of input.

A function MUST NOT return newly allocated memory from a name that implies a
borrowed result.

### 10.3 Allocation families

Allocation and release families MUST match.

- Memory from `g_malloc`, `g_new`, `g_strdup`, `g_memdup2`, `g_string_free(...,
  FALSE)`, and related GLib allocation APIs MUST be released with `g_free`.
- GObjects MUST be released with `g_object_unref` or an appropriate automatic
  cleanup wrapper.
- `GBytes`, `GArray`, `GPtrArray`, `GHashTable`, and other ref-counted/container
  types MUST use their documented unref/free functions.
- Memory returned by POSIX or library APIs MUST use that API's documented release
  function.
- `malloc`/`calloc`/`realloc` MAY be used only inside a module that does not pass
  those allocations to GLib ownership APIs. Such memory MUST be released with
  `free`.
- Allocations MUST NOT cross an interface unless the header states the allocator
  family and matching release function.
- `g_free` and `free` MUST NOT be mixed.

New application-owned allocations SHOULD use GLib allocation APIs for
consistency with the existing GTK code.

### 10.4 Automatic cleanup

Function-local ownership SHOULD use GLib automatic cleanup where it improves
clarity:

- `g_autofree` for GLib-owned memory;
- `g_autoptr(Type)` for types with a cleanup function;
- `g_auto(Type)` for stack wrappers with clear functions;
- `g_steal_pointer` for explicit transfer.

Every new owned project type SHOULD provide a cleanup function and, when
practical, a `G_DEFINE_AUTOPTR_CLEANUP_FUNC` definition.

Automatic cleanup does not remove the need to document ownership at module
interfaces.

### 10.5 Initialization and clearing

Caller-provided structs with owned fields MUST follow an `init`/`clear` contract.

- `init` MUST leave the object valid even when no later operation succeeds.
- `clear` MUST release every owned field.
- `clear` SHOULD be idempotent where practical.
- after `clear`, pointer fields MUST be `NULL` and length/capacity fields MUST be
  zero when the object may be inspected or cleared again;
- partially initialized objects MUST be safe to clear;
- public functions MUST NOT require uninitialized stack garbage as input.

Prefer zero-initializable types. If a type cannot be safely zero-initialized, its
header MUST say so and callers MUST use its initializer.

### 10.6 Move/transfer semantics

C has no language-level move semantics. Shaula MUST model moves explicitly.

When ownership is transferred:

- the API name MUST include `_take`, `_steal`, or equivalent documented transfer;
- the source pointer MUST be cleared immediately after a successful transfer;
- failure behavior MUST state whether ownership was consumed;
- APIs SHOULD consume ownership either always or only on success, not under
  ambiguous conditions;
- constructors named `_new_*_take` MUST document ownership on both success and
  failure.

Prefer "consume always" or "consume on success" contracts consistently within a
module.

### 10.7 Borrowed lifetime rules

Borrowed and interior pointers MUST NOT be stored beyond their documented
lifetime.

In particular:

- borrowed argv and environment strings MUST not be retained after command
  parsing without duplication;
- pointers into `GString`, `GByteArray`, arrays, or hash tables become invalid
  when the container mutates or is freed;
- pointers into parsed JSON/TOML trees MUST not outlive the tree;
- GTK widget pointers stored in state require a live strong owner or a documented
  weak relationship;
- callback user data MUST outlive the signal/async operation or use an explicit
  weak/cancellable mechanism.

### 10.8 Strings and byte buffers

Text and arbitrary bytes MUST be represented distinctly.

- UTF-8 text APIs use NUL-terminated `char *` and MUST reject or safely handle
  invalid UTF-8 according to their contract.
- subprocess stdout/stderr, PNG data, clipboard images, and file contents are
  byte buffers with explicit lengths;
- byte buffers MUST NOT be passed to `strlen`, `%s`, or text parsers before an
  explicit bounded conversion/validation step;
- use `GBytes` for immutable shared byte payloads;
- use `GByteArray` or an explicit `{data, len, capacity}` type for mutable bytes;
- embedded NUL bytes MUST never cause silent truncation;
- output limits MUST be checked before growth/allocation.

Recommended project types:

```c
typedef struct {
  const guint8 *data;
  gsize len;
} ShaulaByteSpan; /* borrowed */

typedef struct {
  guint8 *data;
  gsize len;
} ShaulaOwnedBytes; /* owned; clear with shaula_owned_bytes_clear */
```

### 10.9 Arrays and collections

Collection element ownership MUST be documented.

- `GPtrArray` SHOULD be created with an element free function when it owns
  elements;
- a container MUST NOT sometimes own elements and sometimes borrow them;
- removal operations MUST state whether they destroy, return, or transfer the
  removed element;
- lengths use `gsize`/`size_t` unless an external API requires another type;
- conversion to signed GTK indices or `int` MUST be range-checked;
- array growth MUST check multiplication/addition overflow.

### 10.10 Tagged outcomes and errors

C ports of Zig tagged unions MUST remain tagged structures, not loose nullable
pointer combinations.

Recommended pattern:

```c
typedef enum {
  SHAULA_CAPTURE_OUTCOME_SUCCESS,
  SHAULA_CAPTURE_OUTCOME_FAILURE,
} ShaulaCaptureOutcomeKind;

typedef struct {
  ShaulaCaptureOutcomeKind kind;
  union {
    ShaulaCaptureSuccess success;
    ShaulaCaptureFailure failure;
  } value;
} ShaulaCaptureOutcome;
```

Rules:

- the tag MUST always identify the active union member;
- every tagged type with owned members MUST have a clear function;
- switches over tags SHOULD be exhaustive and have no permissive default branch
  when compiler warnings can enforce completeness;
- output parameters MUST be initialized to a clearable state before work begins;
- on failure, functions MUST either leave outputs unchanged or leave them valid
  and empty as documented;
- errors MUST not be represented by success values containing null internals.

Internal failures SHOULD use a typed project error/result model. `GError` MAY be
used at GLib/GTK seams, but public `ERR_*` mapping remains centralized and MUST
not depend on matching human-readable `GError` strings.

### 10.11 Cleanup flow

Multi-resource functions SHOULD use one of two styles:

1. automatic cleanup variables with explicit `g_steal_pointer` on success; or
2. a single `goto cleanup` path.

`goto cleanup` is permitted and encouraged when it makes ownership obvious.
Deeply nested conditional cleanup and duplicated failure releases SHOULD be
avoided.

Every early return MUST be audited for owned resources, file descriptors, child
processes, locks, temporary files, and GLib sources.

### 10.12 File descriptors and OS handles

File descriptors and process handles are owned resources.

- initialize file descriptors to `-1`;
- close them exactly once;
- set them back to `-1` after close when cleanup may repeat;
- set close-on-exec unless intentional inheritance is part of the helper
  contract;
- handle `EINTR` where required;
- do not leak pipe ends into children;
- temporary files MUST be unlinked on failed operations unless the contract says
  they are diagnostic artifacts;
- lock ownership MUST be explicit and released on every exit path.

### 10.13 GObject ownership

For GTK/GObject code:

- storing a GObject beyond the current call requires an owned reference unless
  the field is explicitly weak;
- strong references use `g_object_ref`/`g_object_unref` or `g_set_object`;
- weak references use `GWeakRef` when callbacks/async work may outlive a window;
- `g_object_add_weak_pointer` MAY be used only when teardown ordering is simple
  and documented;
- signal handler IDs owned by a state object MUST be disconnected during clear
  unless `g_signal_connect_object` provides safe automatic disconnection;
- source IDs from `g_timeout_add`, idle handlers, or similar APIs MUST be removed
  or invalidated during teardown;
- widgets MUST be manipulated on the GTK main thread;
- callbacks MUST not dereference state after window/application destruction.

### 10.14 Asynchronous operations

Every asynchronous operation MUST have an explicit operation/context object that
owns:

- cancellable state;
- strong or weak references needed by completion;
- any copied input;
- partial output;
- exactly-once completion state.

Rules:

- cancellation and failure are distinct outcomes;
- window destruction MUST cancel or safely detach pending work;
- completion callbacks MUST tolerate cancellation and provider changes;
- callbacks MUST not emit duplicate user feedback;
- payload ownership MUST be transferred or released exactly once;
- stale system clipboard payloads MUST not be inserted after provider change;
- shared GTK objects MUST not be accessed from worker threads without a main
  context handoff.

### 10.15 Image and history ownership

Image payloads are high-cost resources and require explicit shared ownership.

- base images and pasted/imported image assets MUST have one documented owner or
  an explicit reference-counted asset object;
- history snapshots MUST NOT accidentally deep-copy immutable pixel payloads;
- annotation clones MUST define whether image pixels are shared or duplicated;
- crop/export operations MUST not mutate shared immutable assets in place;
- size and pixel-count validation MUST happen before allocation;
- multiplication used to compute rowstride or total bytes MUST be overflow
  checked;
- decoded image lifetime MUST not depend on the clipboard provider;
- GDK Pixbuf references MUST follow `g_object_ref`/`g_object_unref` semantics;
- any future shared asset store MUST have deterministic release when documents,
  history entries, and internal clipboard entries disappear.

The accepted Preview clipboard ownership decision in ADR 0001 remains binding.

### 10.16 Globals and static lifetime

Mutable global state is forbidden unless an accepted ADR documents why it is
necessary.

Allowed static data includes:

- immutable lookup tables;
- constant strings;
- compile-time metadata;
- thread-safe one-time initialized immutable caches where lifecycle is process
  lifetime and teardown is unnecessary.

Static mutable buffers, implicit current errors, and hidden singleton state MUST
NOT be introduced.

### 10.17 Nullability

Nullability is part of the interface.

- required pointer arguments SHOULD use assertions/annotations appropriate to
  internal versus public APIs;
- optional pointers MUST be documented;
- nullable return values MUST define whether `NULL` means absence, failure, or
  both;
- failure MUST not be encoded ambiguously as either `NULL` or an empty string;
- use `G_GNUC_WARN_UNUSED_RESULT` for fallible functions where ignoring the
  result would leak or corrupt state;
- use `_Nullable`/`_Nonnull` annotations when supported without harming GCC
  compatibility, or document nullability in headers otherwise.

### 10.18 Integer safety

Every narrowing or signedness conversion at a module seam MUST be reviewed.

- file sizes, buffer lengths, and allocation sizes use `gsize`/`size_t`;
- dimensions MUST be validated before conversion to `int`/GTK types;
- arithmetic for dimensions, pixels, rowstride, and allocation bytes MUST check
  overflow;
- negative external values MUST be rejected before unsigned conversion;
- timestamps and microseconds use explicit 64-bit types;
- exit codes and signals MUST not be conflated;
- enums read from files/JSON MUST be range validated before casting.

### 10.19 Sensitive and temporary data

Shaula is not a secrets manager, but captured paths and pixel data may be
sensitive.

- temporary artifact paths MUST not escape through saved-path fields or copy-only
  notifications;
- diagnostic logs MUST not dump image bytes;
- stdout/stderr fixture capture MUST avoid collecting unrelated desktop content;
- temporary capture files MUST follow existing runtime directory permissions;
- failure cleanup MUST remove incomplete durable outputs unless the contract
  explicitly preserves them;
- tests MUST use dedicated temporary directories and avoid the user's real
  config/history.

## 11. API design rules for migrated C modules

### 11.1 Deep module interfaces

A migrated module SHOULD expose the smallest interface that preserves its current
responsibility. It MUST NOT mirror every internal Zig helper as a public C
function.

The owner named in `CONTEXT.md` remains the owner unless the migration explicitly
updates the ownership map.

### 11.2 Header discipline

Public/internal headers MUST:

- include only required dependencies;
- use include guards;
- forward-declare project types where possible;
- avoid exposing GTK types from non-UI domain modules;
- document ownership and nullability;
- avoid public struct fields unless callers genuinely need representation
  access;
- avoid macros that evaluate arguments more than once;
- keep public `ERR_*` strings out of unrelated headers.

Opaque structs SHOULD be used for stateful modules when representation sharing is
not required.

### 11.3 Error handling

Fallible functions MUST make failure explicit. Preferred forms are:

- a boolean/status plus initialized typed output and typed error;
- a tagged result structure;
- `GError **` only at GLib-like interfaces, followed by deterministic mapping.

Functions MUST NOT:

- print errors as their only error channel;
- depend on global `errno` after calling unrelated functions;
- map errors by parsing human-readable stderr when a typed exit/protocol exists;
- silently downgrade contract failures;
- return partially valid output without a documented degraded result.

### 11.4 Logging

Internal diagnostic logging MUST stay separate from public stdout.

- JSON mode stdout remains exactly one object;
- human CLI output remains controlled by command owners;
- diagnostics SHOULD use a centralized logging facility;
- logs MUST identify module and operation;
- logs MUST not replace structured errors;
- expected cancellation SHOULD not be logged as a severe failure.

## 12. Testing and verification

No Zig module may be deleted until equivalent C behavior is covered.

### 12.1 Unit tests

Every migrated Zig test MUST be:

- ported to a C test;
- replaced by an equal or stronger contract/integration test; or
- explicitly removed with a written explanation that the behavior is obsolete
  under an accepted contract change.

The preferred C unit harness is GLib's `g_test` unless a module requires a more
specialized harness.

### 12.2 Differential tests

During coexistence, differential tests SHOULD execute Zig and C implementations
with identical inputs and compare:

- parsed models;
- JSON semantic output;
- exit codes;
- error codes and retryability;
- generated files;
- config patches;
- helper argv/environment;
- output limits and truncation behavior.

Differences MUST be classified as:

- expected nondeterminism;
- documented bug correction;
- intentional contract change;
- port regression.

Unclassified differences block cutover.

### 12.3 Sanitizers

The maintained C test suite MUST pass under:

- AddressSanitizer;
- UndefinedBehaviorSanitizer.

LeakSanitizer SHOULD run where compatible with GLib/GTK process-lifetime caches.
Known external-library leaks MUST be suppressed narrowly; project-owned leaks
MUST NOT be suppressed.

A sanitizer pass MUST include unit and non-interactive contract tests. GTK helper
smoke tests SHOULD run under sanitizers when the session permits.

### 12.4 Static analysis

The port SHOULD establish:

- `clang-tidy` for selected correctness and bug-prone checks;
- Clang static analyzer or `scan-build`;
- `git diff --check`;
- include dependency checks where practical.

Static analysis findings involving ownership, nullability, bounds, or use after
free block module completion unless proven false positive and locally annotated.

### 12.5 Integration and manual tests

After each behavior-affecting migration step, run:

```bash
./dev check
git diff --check
```

During build coexistence, `./dev check` MUST exercise the implementation selected
for that milestone and MUST clearly report which implementation ran.

For capture, Overlay, Preview, clipboard, GTK, Wayland, Niri, or portal changes,
run the relevant maintained targeted checks. Interactive changes require real
user validation through the existing workflows, including:

```bash
./dev capture
./dev all
```

A helper that merely starts is not considered behaviorally equivalent.

## 13. Commit and review rules

Migration commits SHOULD be small and capability-preserving.

A typical module migration sequence is:

1. characterization tests;
2. C interface and implementation;
3. build wiring;
4. caller cutover;
5. obsolete Zig/test removal;
6. documentation/ownership-map update.

A commit MAY combine these when the module is tiny and the result remains easy to
review.

Each migration pull request or review unit MUST state:

- Zig files replaced;
- C files added/changed;
- callers switched;
- ownership rules introduced;
- tests ported;
- differential results;
- sanitizer results;
- manual checks required/performed;
- known differences;
- rollback method.

Generated translation volume is not evidence of completion.

## 14. Rollback and bisectability

Every phase MUST preserve a path back to the previous working implementation
until its acceptance gates pass.

Preferred rollback mechanisms:

- build-time implementation selection;
- helper environment override;
- side-by-side test executables;
- one-seam caller cutover;
- independent commits.

The migration MUST NOT leave long-lived runtime fallback logic that silently
switches between Zig and C in production. Temporary selection is a development
and verification mechanism only.

## 15. Packaging and release requirements

Before final cutover, verify:

- all canonical helper executables are present;
- installed names and sibling-helper resolution are unchanged;
- desktop files, icons, hicolor cache behavior, and Noctalia assets are installed;
- source package uses the final C/Meson dependencies;
- binary package contains no Zig runtime/build artifacts;
- x86_64 release archive passes install verification;
- aarch64 source build remains supported;
- stripped build behavior is preserved;
- release checksums and AUR metadata are regenerated;
- uninstall removes exactly the installed assets;
- no release script invokes `zig` or expects `zig-out`.

## 16. Documentation requirements

As ownership moves:

- update `CONTEXT.md` after each meaningful phase;
- update `spec/architecture.md` implementation ownership paths;
- update `spec/testing.md` build/test commands;
- update `docs/releasing.md` and package instructions at build cutover;
- update README prerequisites only at final toolchain cutover;
- retain historical ADRs and describe supersession rather than rewriting
  history;
- document all new C ownership-sensitive interfaces in English.

## 17. Definition of module completion

A module is complete only when all of the following are true:

- production callers use the C implementation;
- no obsolete Zig caller remains;
- public behavior matches accepted contracts;
- old tests are ported or superseded;
- new C tests pass normally and under sanitizers;
- ownership and nullability are documented;
- no new compiler warnings exist;
- error mapping remains deterministic;
- relevant differential tests show no unexplained difference;
- `CONTEXT.md` ownership is current;
- rollback is possible through version control without reconstructing mixed code.

## 18. Definition of port completion

The Zig-to-C port is complete only when:

1. `find . -name '*.zig'` finds no maintained production or test sources.
2. `build.zig` and Zig-specific build metadata are removed.
3. CI, development scripts, release scripts, and packages do not install or call
   Zig.
4. Meson builds, tests, installs, and strips all executables.
5. Every maintained test passes in the normal build.
6. The maintained non-interactive suite passes under ASan/UBSan.
7. The full CLI/error/config contract matrix passes.
8. Productive capture benchmarks remain within accepted budgets.
9. Preview, Overlay, Settings, capture, clipboard, portal, and Niri manual checks
   pass on the supported reference environment.
10. Release archive/install verification passes.
11. No temporary mixed-language bridges remain.
12. Documentation and ownership maps describe the C implementation.
13. Repository-wide searches find no stale claims that Shaula requires or is
    implemented in Zig, except historical records explicitly marked as such.

## 19. Historical initial actions

The first implementation work SHOULD be:

1. Commit this specification and ADR separately from existing unrelated working
   changes.
2. Create the baseline migration matrix mapping Zig modules to callers and tests.
3. Introduce a non-authoritative Meson skeleton that builds one isolated test.
4. Port `preview_geometry.zig` and its tests as the first C parity slice.
5. Establish ASan/UBSan CI before porting process execution or configuration.
6. Do not cut over the main CLI until phases 2–7 have proven the supporting
   modules.

The current uncommitted Preview changes predate this migration specification.
They MUST be reviewed and committed or separated intentionally before migration
commits are created, so the port remains bisectable.
