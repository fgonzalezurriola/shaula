# Shaula Product Requirements

Shaula is currently scoped as a Niri-first capture tool with deterministic CLI/JSON contracts. This document intentionally defines the product that exists now, not speculative roadmap items.

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

## Explicit Non-Goals

The following are intentionally not part of the current product contract:

- OCR
- screen recording
- scrolling capture
- annotation/editor workflows
- public UI placeholders for future features
