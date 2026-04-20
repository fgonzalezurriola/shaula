# Shaula Algo Spec (Central Technical Blueprint)

## Executive Technical Summary

Shaula is a high-performance, machine-first instant capture tool for Niri and Wayland. It prioritizes deterministic behavior, low-latency execution, and non-blocking integration with external plugins (e.g., Noctalia). The system is built with Zig 0.16.0, leveraging Niri-specific IPC and Wayland protocols (wlr-screencopy, layer-shell) to achieve sub-150ms capture cycles on the hot path.

## Decision Register

| ID | Decision | Status | Rationale |
| --- | --- | --- | --- |
| D-001 | Niri-first focus | Locked | Optimize for Niri's tile-based workflow and performance primitives. |
| D-002 | Agent-First CLI | Locked | Guarantee machine-readability via deterministic JSON contracts and error tokens. |
| D-003 | Zig 0.16.0 pin | Locked | Stable toolchain with deterministic behavior and error handling. |
| D-004 | Hot-path isolation | Locked | Decouple capture critical path from non-essential processing (Noctalia, OCR). |
| D-005 | Daemon-first architecture | Locked | Centralize state, capabilities, and cross-process orchestration. |
| D-006 | Deterministic error taxonomy | Locked | Map every failure to specific `ERR_*` tokens and recovery actions. |
| D-007 | Performance gates | Locked | Enforce strict p95 latency and resource budgets for hot-path operations. |

## MVP Capability Matrix

| Feature | Path | Status | Risk |
| --- | --- | --- | --- |
| Area Capture | `wlr-screencopy` | MVP | Protocol deprecation (ext-image-copy-capture). |
| Fullscreen Capture | `wlr-screencopy` | MVP | Same as Area Capture. |
| Window Capture | Niri IPC / Heuristics | MVP | Window/tile semantics are evolving in Niri. |
| Selection Overlay | `layer-shell` | MVP | Input/focus edge cases on multi-output. |
| History Store | SQLite / Flat-file | MVP | Storage availability and concurrent access. |
| Clipboard Export | `wl-clipboard` | MVP | External dependency reliability. |
| Scrolling Capture | N/A | Deferred | High implementation uncertainty for v1. |
| OCR | Post-capture Worker | Deferred | Resource pressure, out of MVP scope. |

## AGENT-FIRST CLI

Shaula provides a machine-first interface designed for consumption by LLM agents and automation scripts.

### Core Policy
- **JSON Default**: Commands with `--json` MUST emit a single valid JSON object on stdout.
- **Contract Versioning**: Every response includes `contract_version` (pinned at `1.0.0` for MVP).
- **Deterministic Error Tokens**: Failures MUST include a machine-readable `error.code` (starting with `ERR_`).

### Command Families
- `capture`: Area, fullscreen, and window modes with latency guarantees.
- `daemon`: Lifecycle management (start, status, stop) via Unix socket.
- `capabilities`: Runtime probe for compositor features and fallbacks.
- `history`: Query and retrieval of capture metadata.
- `clipboard`: Import/Export primitives for system integration.
- `errors`: Canonical source for taxonomy and exit code mapping.

## Uncertainty / Pending Verification List

| Item | Status | Risk / Dependency |
| :--- | :--- | :--- |
| `ext-image-copy-capture-v1` | Pending | Migration from deprecated `wlr-screencopy` requires Niri protocol update. |
| Scrolling Capture | Uncertain | No stable Wayland primitive; experimental viewport stitching needed. |
| Niri Window vs Tile ID | Active | Evolving semantics in Niri IPC PR #3731; guarded by degradation. |
| Permission Prompt Latency | Pending | User-mediated portal prompts may exceed latency budget by design. |

## Performance Budgets (p95)
- Overlay selection to first-paint: `<= 75ms` (p95), `<= 110ms` (p99).
- Capture completion: `<= 150ms` (area/fullscreen), `<= 220ms` (window).
- Daemon idle: `<= 0.5%` CPU, `<= 40MB` RSS.

## Risk and Dependency Matrix

| ID | Risk / Dependency | Impact | Mitigation Strategy |
| :--- | :--- | :--- | :--- |
| R-001 | `wlr-screencopy` deprecation | Medium | Prepare migration path to `ext-image-copy-capture-v1`. |
| R-002 | Niri IPC breaking changes | High | Stable capability probing + versioned internal IPC. |
| D-001 | Zig 0.16.0 stability | Low | Pinned toolchain with deterministic build matrix. |
| D-002 | Wayland portal latency | Low | Use as fallback; keep direct Niri path as primary. |

## References

- [Niri Repository](https://github.com/niri-wm/niri)
- [wlr-layer-shell-unstable-v1](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)
- [wlr-screencopy-unstable-v1](https://wayland.app/protocols/wlr-screencopy-unstable-v1)
- [XDG Desktop Portal Screenshot](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)
- [Noctalia Plugin Protocol](https://github.com/noctalia/noctalia) (post-MVP integration)

## Technical Summary

This document consolidates architecture, performance, and testing decisions from Tasks 1-12. It serves as the single source of truth for the Shaula technical implementation. No speculative features are included; all entries reflect current implemented reality.
