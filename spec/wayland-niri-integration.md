# Shaula Wayland/Niri Integration

See [spec/requirements.md](requirements.md) for the product scope and [spec/algo.md](algo.md) for the locked engineering decisions.

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
5. **Scope lock**: unsupported flows are kept out of the public product contract instead of being represented as placeholders.

## MVP Capability Matrix

| Feature | Protocol/Path | Status | Fallback | Risk |
| --- | --- | --- | --- | --- |
| Area capture | `wlr-screencopy-unstable-v1` output region capture | **MVP feasible** | `org.freedesktop.portal.Screenshot` whole screenshot + user-mediated crop (degraded UX) | `wlr-screencopy` is marked deprecated/experimental; medium protocol churn risk |
| Fullscreen capture | `wlr-screencopy-unstable-v1` full output capture | **MVP feasible** | `org.freedesktop.portal.Screenshot` | Same deprecation/churn risk as area capture |
| Window capture (targeted) | Niri IPC screenshot actions (`screenshot-window`, active tile/window semantics discussed in PR #3731) | **MVP feasible with degradation** | Explicit degrade to area capture when target identity/geometry is unresolved | Window/tile semantics are evolving; target ambiguity risk during compositor/runtime transitions |
| Selection overlay | `wlr-layer-shell-unstable-v1` (`overlay` layer + explicit input region / keyboard interactivity rules) | **MVP feasible** | Abort capture with deterministic error if overlay surface cannot be mapped | Focus/input edge cases on multi-output and interactivity modes |
| Permission mediation | Direct compositor path when available (Niri session), otherwise portal-mediated consent | **MVP feasible** | Portal request/response flow (`org.freedesktop.portal.Request`) | User prompt latency and sandbox policy variance |
### Supported Product Surface

- area capture
- fullscreen capture
- window capture with explicit capability checks
- selection overlay
- permission mediation

### Runtime Contract Notes for Niri-first Capture

- Capture command success requires runtime backend execution through the configured helper/process boundary.
- Runtime helper unavailability maps deterministically to `ERR_CAPTURE_BACKEND_UNAVAILABLE`.
- Backend identifier reported in capabilities and capture payloads is canonicalized to `niri-wayland-direct`.
- Implicit copy/preview captures use an internal runtime artifact path. Explicit
  `--save` captures use the configured save folder, defaulting to
  `~/Pictures/shaula`.
- History persistence is post-capture and bounded to Top-N 20 entries, newest-first.

### Overlay Helper STDIO Contract v1

The overlay parent/parser boundary uses a deterministic helper envelope on stdout:

```json
{
  "status": "ok|cancel|error",
  "geometry": { "x": 0, "y": 0, "width": 0, "height": 0 },
  "action": "capture|cancel",
  "error": { "code": "ERR_*", "message": "text" }
}
```

Deterministic parser mapping (`src/overlay/overlay.zig`) to `SelectionResult`:

- `status="ok"` requires `action="capture"` and valid non-zero `geometry`; maps to `cancelled=false` and forwards exact geometry.
- `status="cancel"` maps to `cancelled=true`.
- `status="error"` maps to `cancelled=true` (parent capture command then emits deterministic `ERR_SELECTION_CANCELLED` envelope).
- Malformed/invalid helper payload maps to `cancelled=true` (same deterministic caller behavior).

Contract guardrail: this parser contract does **not** change public `capture area` JSON field names or envelope structure.

## Runtime Constraints

1. `wlr-screencopy-unstable-v1` is documented as deprecated in favor of `ext-image-copy-capture-v1`; this is a migration constraint, not a public feature promise.
2. Niri window vs tile screenshot semantics remain capability-gated and must fail deterministically when unresolved.
3. Portal screenshot API is a fallback path only.
4. Overlay keyboard/focus failures must surface deterministic `ERR_*` outcomes rather than silent retry loops.
5. Fractional scaling can expose logical-vs-physical drift; helper selection
   geometry is compositor/output logical, while crop/redaction/export operate on
   physical PNG pixels after normalization.
6. Overlay teardown timing can still affect live capture if the compositor has
   not repainted after Shaula's layer surface exits. Frozen region capture crops
   from the frozen source image; live capture still needs manual Niri
   verification.

## Optional Noctalia Plugin Integration (post-MVP)

- Noctalia integration is **non-blocking** and optional. It is not part of the primary Wayland/Niri capture path.
- The plugin uses an adapter model to map these MVP actions to Shaula CLI operations: `capture-area`, `capture-fullscreen`, `capture-window`, `open-last`, `history`.
- The plugin invokes public Shaula CLI commands for optional shell actions; it does not depend on a resident daemon or private socket protocol.
- If the plugin is absent, disabled, or fails to launch a command, terminal and keybinding capture flows remain unchanged and fully functional.
- Failure domain separation is mandatory: plugin UI failures do not mutate core capture contract semantics.
