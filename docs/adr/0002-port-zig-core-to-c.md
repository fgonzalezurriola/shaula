# ADR 0002: Port the Zig Core to C

- Status: Accepted and implemented
- Date: 2026-07-10
- Decision owners: Shaula maintainers
- Detailed migration contract: [`spec/zig-to-c-port.md`](../../spec/zig-to-c-port.md)

## Context

At the time of this decision, Shaula was a mixed Zig and C application. The CLI,
capture lifecycle, configuration, runtime orchestration, capability logic, and
several C-facing bridge modules were implemented in Zig. Preview, Overlay,
Settings, rendering, gestures, and most native GTK behavior were implemented in
C.

The decision has now been fully implemented. All maintained production and test
code is C, Meson is the sole build system, and Zig remains referenced here only
as historical migration context.

Maintaining the mixed toolchain requires Zig-specific build logic, object
bridges, release setup, version pinning, and ownership contracts across the
Zig/C ABI. The project has decided to remove Zig and converge on C while
preserving the existing native GTK implementation and process topology.

The migration affects implementation language and build ownership. It does not
change Shaula's product behavior, machine-first CLI contract, Niri-first capture
model, short-lived helper topology, or public error taxonomy.

## Decision

Shaula will port all maintained Zig production and test code to C.

The port will be incremental and contract-preserving:

- existing C GTK helpers remain in C;
- temporary C ABI seams may be used while modules coexist;
- public CLI, helper, JSON, error, configuration, and performance contracts stay
  authoritative;
- Meson becomes the final build system;
- each Zig module is removed only after its C replacement has equivalent tests,
  documented ownership, and sanitizer coverage;
- the final repository will not require Zig to build, test, install, package, or
  release Shaula.

The normative migration phases and memory-management rules live in
`spec/zig-to-c-port.md`.

## Consequences

### Positive

- Shaula converges on one implementation language.
- Existing Preview, Overlay, and Settings code does not need to be rewritten.
- Zig/C bridge objects and Zig-specific release tooling can be removed.
- GTK, GLib, Cairo, Pango, GDK Pixbuf, and layer-shell APIs are used directly
  from their native language.
- Modules can be migrated independently through existing process and ABI seams.
- The final build and packaging model becomes conventional for a C/GTK project.

### Negative

- The core loses Zig's compiler-enforced slices, error unions, tagged unions,
  optionals, and allocator discipline.
- More code becomes memory-unsafe and dependent on explicit review conventions.
- Existing Zig tests must be translated or superseded.
- Configuration, process execution, JSON output, and capture lifecycle require
  especially careful cleanup and error handling in C.
- The migration consumes development time without directly adding product
  features.

### Risk controls

The port requires:

- explicit ownership vocabulary and naming conventions;
- matched allocation/free families;
- clearable partially initialized structs;
- tagged C outcomes instead of nullable-pointer encodings;
- ASan and UBSan gates;
- differential Zig/C tests during coexistence;
- preserved public contract fixtures;
- small, bisectable migration commits;
- no deletion of Zig modules before their callers and tests have cut over.

## Alternatives considered

### Keep the mixed Zig/C implementation

Rejected. The project has decided to remove Zig from its maintained toolchain.

### Rewrite the entire application in Rust

Rejected for this migration. A Rust rewrite would require reproducing the large
existing C GTK editor and interaction surface, substantially increasing scope,
behavioral risk, and time to parity.

### Port Zig to C and later rewrite immediately in Rust

Rejected as the migration plan. It would pay for two translations and two parity
cycles. This ADR treats C as the intended implementation, not a disposable
intermediate representation.

### Rewrite only the main CLI and retain Zig bridge objects

Rejected as an end state. It would leave Zig in the build and preserve much of
the mixed-language complexity this decision is intended to remove.

## Supersession

A future language rewrite requires a new ADR. It must not weaken or bypass the
public behavior and ownership contracts established during this port without an
explicit replacement decision.
