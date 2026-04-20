# Shaula Wayland/Niri Integration

See [spec/algo.md](algo.md) for the central technical blueprint and performance budgets.

## Scope and Strategy

Shaula v1 is **Niri-first only**. The primary capture strategy is to rely on Niri/Wayland capabilities for fast-path capture modes, and to use xdg-desktop-portal screenshot semantics only as an explicit fallback path when direct capture paths are unavailable or policy-constrained.

Operational lock for this integration spec:

- Runtime capture path is real backend execution, no productive stub success path.
- capabilities strict contract is enforced before backend execution.
- Overlay remains v1 base scope for selection only.
- Shell artifact precondition guard runs before capture with bounded timeout behavior.
- Noctalia remains optional and adapter-based, outside capture hot-path dependencies.

Primary sources used for this spike:

- Niri repository (feature surface and protocol support): https://github.com/niri-wm/niri
- Niri screenshot tile/window semantics discussion: https://github.com/niri-wm/niri/pull/3731
- `wlr-layer-shell-unstable-v1` protocol (overlay surfaces and input semantics): https://wayland.app/protocols/wlr-layer-shell-unstable-v1
- `wlr-screencopy-unstable-v1` protocol (capture semantics and deprecation notice): https://wayland.app/protocols/wlr-screencopy-unstable-v1
- XDG portal screenshot interface (`org.freedesktop.portal.Screenshot`): https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html

## Niri Spike Exit Criteria

Go/No-Go for consolidating MVP backend decisions:

1. **Compositor target lock**: environment is explicitly treated as Niri-only in v1 docs/contracts.
2. **Capture modes lock**: MVP supports deterministic `area` and `fullscreen`; `window` is allowed only with explicit degradation semantics.
3. **Overlay feasibility lock**: layer-shell overlay path is documented with explicit input/keyboard handling constraints.
4. **Fallback lock**: portal screenshot path is documented as fallback, not primary.
5. **Uncertainty lock**: unsupported or unstable paths (scrolling capture, recording, OCR) are explicitly tagged as uncertain/experimental or deferred.

## MVP Capability Matrix

| Feature | Protocol/Path | Status | Fallback | Risk |
| --- | --- | --- | --- | --- |
| Area capture | `wlr-screencopy-unstable-v1` output region capture | **MVP feasible** | `org.freedesktop.portal.Screenshot` whole screenshot + user-mediated crop (degraded UX) | `wlr-screencopy` is marked deprecated/experimental; medium protocol churn risk |
| Fullscreen capture | `wlr-screencopy-unstable-v1` full output capture | **MVP feasible** | `org.freedesktop.portal.Screenshot` | Same deprecation/churn risk as area capture |
| Window capture (targeted) | Niri IPC screenshot actions (`screenshot-window`, active tile/window semantics discussed in PR #3731) | **MVP feasible with degradation** | Explicit degrade to area capture when target identity/geometry is unresolved | Window/tile semantics are evolving; target ambiguity risk during compositor/runtime transitions |
| Selection overlay | `wlr-layer-shell-unstable-v1` (`overlay` layer + explicit input region / keyboard interactivity rules) | **MVP feasible** | Abort capture with deterministic error if overlay surface cannot be mapped | Focus/input edge cases on multi-output and interactivity modes |
| Permission mediation | Direct compositor path when available (Niri session), otherwise portal-mediated consent | **MVP feasible** | Portal request/response flow (`org.freedesktop.portal.Request`) | User prompt latency and sandbox policy variance |
| Scrolling capture | No stable Niri/Wayland primitive in current spike scope | **Uncertain / experimental** | Defer to post-MVP contract definition | High implementation uncertainty, likely compositor-specific heuristics |
| Screen recording | Not in this spike’s validated capture path | **Deferred** | None in MVP | Out of MVP scope; requires separate protocol/runtime decisions |
| OCR pipeline | Post-capture processing outside hot path | **Deferred** | None in MVP | Would add CPU/memory latency pressure if not isolated |

### MVP vs Experimental vs Deferred (explicit)

- **MVP feasible now**: area capture, fullscreen capture, selection overlay, permission mediation, window capture with explicit degradation path.
- **Uncertain/experimental**: scrolling capture (no stable, primary-source-backed path locked for v1).
- **Deferred**: screen recording and OCR integration (post-MVP by design to protect hot path).

### Runtime Contract Notes for Niri-first Capture

- Capture command success requires runtime backend execution through the configured helper/process boundary.
- Runtime helper unavailability maps deterministically to `ERR_CAPTURE_BACKEND_UNAVAILABLE`.
- Backend identifier reported in capabilities and capture payloads is canonicalized to `niri-wayland-direct`.
- Default output behavior for capture artifacts is aligned to `~/Pictures/Shaula` when no explicit output path is provided.
- History persistence is post-capture and bounded to Top-N 20 entries, newest-first.

## Uncertainties

1. `wlr-screencopy-unstable-v1` is explicitly documented as deprecated in favor of `ext-image-copy-capture-v1`; this creates medium-term migration risk.
2. Niri window vs tile screenshot semantics are still active design surface (PR #3731), so “window-like” capture behavior must be guarded by runtime capability checks and explicit degradation.
3. Portal screenshot API is straightforward but introduces interactive/user-mediation variability; deterministic AGENT-FIRST flows should treat portal paths as fallback only.
4. Overlay keyboard/focus behavior depends on layer-shell interactivity modes and compositor policy; failures must surface deterministic `ERR_*` outcomes rather than silent retry loops.

## Optional Noctalia Plugin Integration (post-MVP)

- Noctalia integration is **non-blocking** and optional. It is not part of the primary Wayland/Niri capture path.
- The plugin uses an adapter model to map these MVP actions to Shaula CLI operations: `capture-area`, `capture-fullscreen`, `capture-window`, `open-last`, `history`.
- The plugin communicates with daemon over versioned Unix-socket IPC (`ipc_version: 1.0.0`) only for non-critical optional flows.
- If plugin IPC fails, times out, is absent, or version-mismatched, capture flow behavior for `area`/`fullscreen` remains unchanged and fully functional.
- Failure domain separation is mandatory: plugin adapter failures emit deterministic plugin-specific error tokens and do not mutate core capture contract semantics.
