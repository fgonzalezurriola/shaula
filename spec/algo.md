# Shaula Algo Spec (Central Technical Blueprint)

See [spec/requirements.md](requirements.md) for product direction and [spec/performance.md](performance.md) for numeric budgets. This document keeps the locked engineering choices, runtime constraints, and recovery rules in one place.

## Executive Technical Summary

Shaula is a machine-first, Niri-first screenshot tool written in Zig. The product direction lives in [spec/requirements.md](requirements.md); this file keeps the implementation contracts that make the product deterministic, low-latency, and easy to automate.

## Decision Register

| ID | Decision | Status | Rationale |
| --- | --- | --- | --- |
| D-001 | Niri-first focus | Locked | Optimize for Niri's tile-based workflow and runtime contracts. |
| D-002 | Agent-first CLI | Locked | Guarantee machine-readability via deterministic JSON contracts and error tokens. |
| D-003 | Zig 0.16.0 pin | Locked | Stable toolchain with deterministic behavior and error handling. |
| D-004 | Hot-path isolation | Locked | Keep capture separate from non-essential work such as preview, history, and exports. |
| D-005 | Daemon-first architecture | Locked | Centralize state, capabilities, and cross-process orchestration. |
| D-006 | Deterministic error taxonomy | Locked | Map every failure to specific `ERR_*` tokens and recovery actions. |
| D-007 | Performance gates | Locked | Keep the hot path inside the budgets in [spec/performance.md](performance.md). |

## MVP Capability Matrix

| Feature | Path | Status | Risk |
| --- | --- | --- | --- |
| Area capture | `wlr-screencopy` | MVP | Protocol deprecation path to `ext-image-copy-capture-v1`. |
| Fullscreen capture | `wlr-screencopy` | MVP | Same risk as area capture. |
| Window capture | Niri IPC / heuristics | MVP | Window/tile semantics are still evolving in Niri. |
| Selection overlay | `layer-shell` | MVP | Input and focus edge cases on multi-output. |
| Clipboard export | `wl-clipboard` | MVP | External dependency reliability. |
| History store | File-backed | MVP | Storage availability and concurrent access. |
## AGENT-FIRST CLI

Shaula is machine-first. `--json` emits one JSON object on stdout, every response includes `contract_version`, and every failure carries a deterministic `ERR_*` code. The public grammar is documented in [spec/architecture.md](architecture.md).

Command families:

- `capture`
- `preview`
- `config`
- `daemon`
- `capabilities`
- `history`
- `clipboard`

## Runtime Constraint List

- `ext-image-copy-capture-v1` is a future migration path, not a public promise.
- Niri window vs tile identity remains capability-gated and must fail deterministically when unresolved.
- Portal or permission-mediated paths are fallback-only and may exceed the hot-path budget.
- Optional integrations must never block capture completion.

## Performance Budgets

See [spec/performance.md](performance.md) for the numeric targets. The only contract statement here is that the capture hot path must stay inside those budgets and avoid unnecessary allocations or blocking work.

## Uncertainty / Pending Verification List

- Focused output semantics through Niri/Wayland capability probing.
- Focused window identity and tile/window ambiguity through Niri IPC.
- Pinning behavior under Niri layer-shell or compositor-supported window rules.
- Pixelate, blur, and solid-bar redaction determinism for QA/release gates.
- Ruler and color picker behavior across logical versus physical pixels and fractional output scale.
- OCR, QR reading, uploads, smart selection, object removal, and scrolling screenshot remain optional work.

## Risk and Dependency Matrix

| ID | Risk / Dependency | Impact | Mitigation Strategy |
| :--- | :--- | :--- | :--- |
| R-001 | `wlr-screencopy` deprecation | Medium | Keep the migration path to `ext-image-copy-capture-v1` visible. |
| R-002 | Niri IPC breaking changes | High | Stable capability probing plus versioned internal IPC. |
| R-003 | Zig toolchain drift | Low | Keep the toolchain pinned in the build matrix. |
| R-004 | Portal latency | Low | Use fallback-only paths and keep direct Niri capture primary. |

## References

- [Niri Repository](https://github.com/niri-wm/niri)
- [wlr-layer-shell-unstable-v1](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)
- [wlr-screencopy-unstable-v1](https://wayland.app/protocols/wlr-screencopy-unstable-v1)
- [XDG Desktop Portal Screenshot](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html)
- [Noctalia Plugin Protocol](https://github.com/noctalia/noctalia) (post-MVP integration)

## Technical Summary

This document is the locked implementation register. Product scope lives in [spec/requirements.md](requirements.md); performance lives in [spec/performance.md](performance.md); the rest of the folder breaks the contract surface down by subsystem.
