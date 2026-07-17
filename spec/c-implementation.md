# C Implementation Contract

This document defines the active safety, ownership, interface, and verification
rules for Shaula's maintained C implementation. Product behavior remains
defined by the other files in `spec/`; ADR-0002 records why the project uses C.

## Build and runtime

- Meson is the sole production and test build definition.
- Production C targets C11 or later and compiles with the repository warning
  policy. Warning suppressions must be local and tied to an external interface.
- Shaula remains a short-lived CLI plus helper processes. Do not introduce a
  resident daemon without a new architectural decision.
- Process execution uses direct argv arrays. Public behavior must never depend
  on shell interpolation.
- Helper resolution remains deterministic: explicit environment override,
  sibling helper, fixed system candidates where defined, then `PATH`.
- Public CLI, JSON, helper protocol, configuration, performance, and `ERR_*`
  contracts remain authoritative across internal refactors.

## Module interfaces

- Expose the smallest interface that preserves a module's responsibility. Do
  not publish internal helpers merely to make them directly testable.
- Public headers include only required dependencies and document ownership,
  nullability, borrowed lifetimes, transfer behavior, and clear functions.
- Runtime process, IO, and protocol functions return typed outcomes. Expected
  failures must not be encoded as ambiguous `NULL` values.
- The public error taxonomy remains centralized. Unknown internal failures map
  to deterministic `ERR_UNKNOWN_UNMAPPED` rather than inventing local codes.
- Diagnostic output belongs on stderr. Helper stdout is protocol data only.

## Ownership vocabulary

Every pointer crossing a module interface has one ownership category:

- **owned**: the receiver must release it;
- **borrowed**: valid only for the documented lifetime;
- **transferred**: ownership moves from caller to callee;
- **shared**: reference-counted ownership;
- **interior**: aliases storage owned by another object.

Public headers document ownership where naming does not make it unambiguous.
Use these suffixes consistently:

- `_new` and `_dup` return owned values;
- `_ref` returns a new shared reference;
- `_borrow` returns borrowed storage;
- `_take` consumes an argument on both success and failure unless documented
  otherwise;
- `_steal` transfers ownership out and clears the source;
- `_clear`, `_free`, and `_unref` release their matching ownership families.

## Allocation and cleanup

- Match allocation families: GLib allocations use `g_free`, GObjects use
  `g_object_unref`, and library objects use their documented release function.
- Do not mix `free` and `g_free` for the same allocation family.
- Prefer `g_autofree`, `g_autoptr`, `g_auto`, and `g_autoptr(GError)` for local
  ownership when their cleanup contract is exact.
- Caller-provided structures with owned fields use an `init`/`clear` contract.
  Partial initialization must remain clearable, and repeated clear calls must
  be safe.
- After clear, owned pointers are `NULL` and lengths/capacities are zero.
- Every early return must be audited for memory, file descriptors, child
  processes, temporary files, locks, and GSources.
- `goto cleanup` is acceptable when it makes ownership more obvious.

## Borrowing, text, and collections

- Borrowed argv and environment strings are not retained after a command.
- Interior pointers into parsed JSON or TOML do not outlive the parsed owner.
- Callback data outlives the callback or is protected by an explicit weak or
  cancellable context.
- NUL-terminated text and arbitrary bytes are distinct interfaces. Binary
  buffers are never passed to text parsers before explicit validation.
- Embedded NUL bytes must not cause silent truncation.
- Collection element ownership is fixed and documented. Removal semantics state
  whether elements are destroyed, returned, or transferred.
- Array growth and allocation arithmetic check overflow before allocation.

## Tagged outcomes and nullability

- Variant data uses a tag that always identifies the active union member.
- Tagged structures with owned members provide a clear function.
- Output parameters are initialized to a clearable state before fallible work.
- On failure, outputs remain unchanged or valid and clearable.
- Optional pointers document whether `NULL` means absence or failure. Empty
  strings are not interchangeable with missing values.
- Signedness and narrowing conversions are validated at module seams.

## Processes and operating-system resources

- The process-execution module owns child creation, bounded stdout/stderr,
  binary stdin, termination classification, and guaranteed child reaping.
- File descriptors are closed on every path. A child that fails after fork is
  terminated and reaped before return.
- Temporary capture files use the runtime directory policy and are removed on
  failed operations unless the public contract requires persistence.
- Capture-session lock ownership is explicit and released before Preview.
- Durable writes preserve backups and use atomic replacement where the config
  contract requires it.

## GTK, GObject, and asynchronous work

- Widgets are manipulated only on the GTK main thread.
- Storing a GObject beyond the current call requires an owned reference unless
  the owning widget tree is explicitly documented.
- Signal handler and GSource IDs owned by state are disconnected or removed
  during cleanup.
- Asynchronous operations use an explicit context with cancellation and a weak
  window/application reference where UI lifetime can end first.
- Completion callbacks tolerate cancellation and provider changes and release
  payload ownership exactly once.
- ADR-0001 remains authoritative for Preview clipboard and Image annotation
  ownership.

## Images and integer safety

- Decoded image lifetime never depends on the clipboard provider.
- Image dimensions and pixel counts are validated before allocation.
- Rowstride, dimensions, and allocation products check overflow.
- Crop/export operations do not mutate shared immutable image data in place.
- History snapshots and annotation clones document whether image pixels are
  shared or duplicated.

## Verification

Every code change runs:

```bash
./dev check
git diff --check
```

Strict warning and sanitizer lanes are:

```bash
./dev strict-check
./dev sanitize-check
```

The maintained suites cover module interfaces and executable contracts. Tests
must use dedicated temporary directories where the runtime supports them; fixed
compatibility paths must be backed up and restored. Tests never mutate a user's
live config, system clipboard, compositor state, or screenshots. Host-sensitive
GTK, Wayland, Niri, overlay, capture, and clipboard behavior remains a targeted
manual gate.

Use ASan and UBSan for changed ownership-sensitive runtime modules. Public JSON
tests compare semantic values and contract-required ordering, not timestamps,
allocator addresses, or incidental temporary paths.
