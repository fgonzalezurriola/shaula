# Shaula Product Requirements

Shaula is scoped as a Niri-first capture tool with deterministic CLI/JSON contracts. This document separates the supported product contract from candidate direction so future work stays Linux/Niri-first instead of mirroring macOS-only capture tools.

## Supported Surface

| Capability | Status | Contract Notes |
| :--- | :--- | :--- |
| All-in-one capture | Supported initial iteration | Uses area capture backend with persisted toolbar UI state and deterministic helper failures. |
| Area capture | Supported | Interactive selection with deterministic `ERR_*` outcomes. |
| Fullscreen capture | Supported | Runtime backend only, no productive stub success path. |
| Window capture | Supported with capability gating | Unsupported runtimes fail deterministically. |
| Previous area capture | Supported | Reuses the last confirmed area rectangle with deterministic `ERR_PREVIOUS_AREA_UNAVAILABLE`. |
| Clipboard copy/import | Supported | `clipboard copy-image` and `clipboard import-image`. |
| History list/show | Supported | Top-N 20, newest-first, stable `latest` alias. |
| Output path default | Supported | Defaults to `~/Pictures/Shaula`. |
| Overlay helper contract | Supported | Helper protocol maps deterministically to `SelectionResult`. |
| Overlay unavailability | Supported | Missing native helper fails deterministically with `ERR_OVERLAY_UNAVAILABLE`. |
| Noctalia adapter | Optional | Never part of the capture hot path. |

## Locked Operational Contracts

- Runtime capture success must come from runtime backend execution, not stub artifact generation.
- Forced stub backend usage is a deterministic failure path: `ERR_CAPTURE_BACKEND_UNAVAILABLE`.
- `capabilities` remains a strict contract: when `capabilities.capture.<mode>=false`, `capture <mode>` cannot succeed.
- Default output directory is `~/Pictures/Shaula`; invalid or unwritable targets fail with `ERR_OUTPUT_PATH_INVALID`.
- History retention is Top-N 20 entries, newest-first, with deterministic trimming on write.
- The public overlay scope is limited to selection, aspect constraint, and capture/cancel actions.
- `previous-area` depends on runtime selection state and never fabricates geometry when no valid area has been confirmed yet.
- Missing interactive backends must not silently degrade into fake successful area selections.
- The shell-artifact precondition runs before capture and may fail with deterministic `ERR_CAPTURE_PRECONDITION_TIMEOUT`.

## Product Direction

Shaula should copy the useful philosophy of fast capture tools, not their platform-specific feature lists. Shottr is the quality bar for selection precision, interaction speed, and post-capture usefulness, while Shaula remains a Linux/Niri-first product. Candidate work must remain valid for Linux, Wayland, and Niri, and must preserve deterministic CLI/JSON contracts before it becomes public surface.

Differentiators:

- Niri IPC and Wayland-first capture paths.
- Polished selection overlay with precise geometry controls.
- Pin screenshots on screen when compositor behavior allows it.
- Pixelate, blur, and solid-bar redaction for sensitive information.
- Frontend/dev tools: ruler, manual measurement, color picker, and average-area color.
- File-first configuration through TOML.

## Incremental UX Strategy

Shottr-level UX is the quality target, but Shaula should reach it through low-risk improvements before redesigning the current UI. The first pass should prefer changes that improve confidence, speed, and precision without replacing the overlay architecture or adding a full editor surface.

Low-hanging fruit:

- Make selection feedback clearer: size badge, coordinates, handle affordances, and stable toolbar placement.
- Tighten keyboard behavior: Esc cancel, Enter confirm, arrow-key nudging, and predictable focus handling.
- Improve repeatability: last-region reuse, persisted toolbar state, and explicit previous-area failures.
- Polish output defaults: save directory, filename templates, copy-image behavior, and copy-path behavior.
- Add small dev-facing utilities where the current capture pipeline can support them cleanly, such as color picking and manual measurement.

Deferred heavier UX work:

- Replacing the overlay visual model.
- Full post-capture preview/editor.
- Annotation tools beyond simple geometry.
- Pinning screenshots if compositor behavior requires a separate window/layer strategy.
- Pixelate/redaction if it requires a new image-editing pipeline.

## Candidate Feature Priorities

### v0: Solid Capture Base

| Priority | Candidate | Contract Gate |
| :--- | :--- | :--- |
| High | Fullscreen capture | Runtime backend success only. |
| High | Region capture | Overlay must emit deterministic selection or cancellation. |
| High | Focused output capture | Capability-gated for Niri/Wayland runtime availability. |
| High | Focused window capture via Niri IPC | Deterministic failure when target identity is unresolved. |
| High | Save to file | Defaults to `~/Pictures/Shaula`; invalid paths fail deterministically. |
| High | Copy image to clipboard | Clipboard failures remain explicit and machine-readable. |
| High | TOML configuration | Config errors must map to stable `ERR_*` outcomes before being public contract. |
| High | Decent selection overlay | Esc cancel and Enter confirm are required interactions. |
| Medium | Visible selection size | Must not change capture JSON shape without contract versioning. |
| Medium | Repeat last region | Must use last confirmed geometry only. |

### v1: Differentiating UX

| Priority | Candidate | Contract Gate |
| :--- | :--- | :--- |
| High | Floating post-capture preview | Must stay outside capture hot-path completion. |
| High | Basic editor with crop | Editor failures must not corrupt the original artifact. |
| High | Pixelate/redaction | Redaction output must be deterministic for release QA. |
| High | Arrows and rectangles | Annotation data model must be versioned before persistence. |
| High | Pin screenshot | Must be capability-gated for Wayland/Niri behavior. |
| Medium | Color picker | Clipboard/export behavior must be explicit. |
| Medium | Magnifier | Shared with overlay/editor when possible. |
| Medium | Ruler/manual measurement | Must support logical/physical pixel clarity. |
| Medium | Simple history | Retention and ordering remain deterministic. |
| Medium | Configurable file naming | Invalid templates must fail deterministically. |

### v2: Power User

| Priority | Candidate | Contract Gate |
| :--- | :--- | :--- |
| Medium | Region OCR | External engine/service availability must be optional and explicit. |
| Medium | QR reader | Decode failures must be non-destructive. |
| Medium | Pretty export with padding, shadow, and rounded corners | Export presets stay outside the capture hot path. |
| Medium | Combine screenshots | Canvas model must be versioned. |
| Medium | S3-compatible upload | Upload is optional worker/plugin surface, never capture-critical. |
| Low | Smart selection | Requires reliable edge/component detection before public exposure. |
| Low | Remove object | Requires an explicit image-editing strategy. |
| Low | Scrolling screenshot | Deferred until Wayland/Niri strategy is clear. |

## Explicit Non-Goals

The following are intentionally not part of the current supported product contract:

- screen recording
- public UI placeholders for future features
