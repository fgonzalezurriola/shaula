---
name: zig-best-practices
description: Zig 0.16.0 best practices for type-safe design, memory handling, build.zig workflow, and modern tooling conventions.
compatibility: opencode
license: MIT
metadata:
  language: zig
  zig_version: "0.16.0"
  audience: developers
---

# Zig Best Practices (0.16.0)

Use this skill when writing or reviewing Zig codebases targeting **Zig 0.16.0**.

This guide preserves the spirit of the original zig-best-practices skill and updates key parts that changed in recent Zig releases (notably C interop workflow, newer type-construction guidance, package workflow, and dev loop ergonomics).

---

## Core Principles

- Make illegal states unrepresentable (`union(enum)` + explicit error sets).
- Keep ownership obvious (allocator and writer/reader passed explicitly).
- Prefer compile-time guarantees over runtime checks when feasible.
- Keep build, test, and format steps deterministic and repeatable.

---

## Type System Patterns

### 1) Prefer `union(enum)` for state machines

```zig
const RequestState = union(enum) {
    idle,
    loading,
    success: []const u8,
    failure: ParseError,
};
```

This prevents impossible combinations that would be possible with multiple optionals in a struct.

### 2) Use explicit error sets in library code

```zig
const ParseError = error{ InvalidSyntax, UnexpectedToken, EndOfInput };

fn parse(input: []const u8) ParseError!Ast {
    // ...
}
```

Avoid `anyerror` in domain APIs unless you're intentionally forwarding arbitrary failures.

### 3) Use distinct domain IDs

```zig
const UserId = enum(u64) { _ };
const OrderId = enum(u64) { _ };
```

This catches accidental ID mixing at compile time.

### 4) Validate config at comptime

```zig
fn Buffer(comptime size: usize) type {
    if (size == 0) @compileError("Buffer size must be > 0");
    return struct {
        data: [size]u8 = undefined,
        len: usize = 0,
    };
}
```

### 5) Zig 0.16.0 type construction update

Prefer specialized builtins for type construction (`@Struct`, `@Enum`, `@Int`, etc.) and avoid relying on legacy `@Type`-centric patterns.

---

## Memory & Resource Management

- Pass allocators to every allocating function.
- Use `defer` immediately after acquisition for unconditional cleanup.
- Use `errdefer` for rollback on error paths.
- Use arenas for short-lived batch allocations.
- In tests, prefer `std.testing.allocator` to surface leaks with traces.

```zig
fn createResource(allocator: std.mem.Allocator) !*Resource {
    const resource = try allocator.create(Resource);
    errdefer allocator.destroy(resource);

    resource.* = try initializeResource();
    return resource;
}
```

---

## I/O Design in Modern Zig

Design APIs around **interfaces** (reader/writer style) instead of hardwiring concrete file types whenever practical.

- Accept readers/writers in functions that process streams.
- Keep I/O dependency injection symmetric with allocator injection.
- Keep parsing/transforms pure where possible; isolate side effects at boundaries.

---

## Build System & C Interop (Updated for 0.16.0)

### Prefer build.zig driven C translation

Move C header translation into `build.zig` (`addTranslateC`) instead of source-level `@cImport` usage. This improves caching, reproducibility, and cross-target behavior.

High-level workflow:

1. Declare translated C step in `build.zig`.
2. Add include paths / macros there.
3. Import generated Zig module from your code.

This keeps C interop policy in one place and simplifies review.

---

## Project & Package Workflow

- Keep dependency state predictable and commit what your team needs for reproducible builds.
- Use project-local package workflow (`zig-pkg/`) intentionally.
- For local dependency iteration, use the `--fork` workflow to override dependencies without rewriting long-term config.

---

## Main Function & CLI Ergonomics

When appropriate, use the modern main signature style for cleaner CLI setup:

```zig
pub fn main(allocator: std.mem.Allocator, args: []const []const u8) !void {
    _ = args;
    // ...
}
```

This removes boilerplate and keeps allocator ownership explicit.

---

## Conventions That Scale

- Prefer `const` over `var`.
- Prefer slices over raw pointers in public APIs where possible.
- Prefer `comptime T: type` over `anytype` unless true ad-hoc polymorphism is intended.
- Use exhaustive `switch` and reserve `unreachable` for truly impossible states.
- Use scoped logging (`std.log.scoped(.module_name)`) for larger systems.

---

## Testing, Formatting, and Dev Loop

### Always run

```bash
zig fmt .
zig test src/root.zig
zig build test
```

### Fast iteration (0.16.0)

```bash
zig build --watch -fincremental
```

Use incremental watch mode for fast local feedback while preserving full test/build checks before merging.

---

## Common Pitfalls

- Hiding allocation internally without clear ownership docs.
- Returning overly broad errors from stable APIs.
- Mixing transport/I/O concerns with core domain logic.
- Treating build.zig as incidental; in Zig, build.zig is part of the product.
- Leaving C interop split across source files and build config.

---

## Minimal Review Checklist (for PRs)

1. Are states encoded with enums/tagged unions instead of nullable field combos?
2. Are error sets explicit at API boundaries?
3. Is allocator ownership obvious and cleanup paired (`defer`/`errdefer`)?
4. Is C interop configured in `build.zig` rather than ad-hoc imports?
5. Are `zig fmt`, tests, and build steps documented and passing?

---

## References

- Zig 0.16.0 release notes: https://ziglang.org/download/0.16.0/release-notes.html
- Zig 0.16.0 language reference: https://ziglang.org/documentation/0.16.0/
- Zig 0.16.0 standard library docs: https://ziglang.org/documentation/0.16.0/std/
