# Shaula Future Feature Contracts

This document specifies the future-feature matrix, aligning with Shaula's Agent-First CLI and hot-path isolation principles. See [spec/algo.md](algo.md) for the central technical blueprint.

## MVP Capability Matrix

| Capability | MVP v1 Status | Notes |
| :--- | :--- | :--- |
| Area capture | Supported | Runtime backend only, no productive stub success path.
| Fullscreen capture | Supported | Deterministic AGENT-FIRST JSON envelope with runtime backend parity.
| Window capture | Degraded/conditional | capabilities strict contract, unsupported modes fail deterministically.
| Clipboard copy/import | Supported | Machine-first `clipboard copy-image` and `clipboard import-image`.
| History list/show | Supported | Top-N 20 newest-first with stable `latest` alias.
| Output path default | Supported | Default destination is `~/Pictures/Shaula` when `--output` is omitted.
| Overlay v1 base | Supported | Gray dimming and base area selection with confirm/cancel.
| Shell artifact guard | Supported | Pre-capture handshake + bounded settle fallback before backend capture.
| Noctalia plugin | Optional MVP adapter | Optional and never in capture hot path.

## Locked Operational Contracts (MVP v1)

- Runtime capture success must come from runtime backend execution, not from stub artifact generation.
- Forced stub backend usage is a deterministic failure path (`ERR_CAPTURE_BACKEND_UNAVAILABLE`).
- capabilities strict contract is mandatory: when `capabilities.capture.<mode>=false`, `capture <mode>` cannot succeed.
- Default output directory is `~/Pictures/Shaula`; invalid/unwritable path fails with `ERR_OUTPUT_PATH_INVALID`.
- History retention is Top-N 20 entries, newest-first, deterministic trimming on write.
- Overlay scope remains v1 base only, no advanced editor/annotation scope in this plan.
- Noctalia integration is optional and adapter-based, action mapping only, capture logic stays in Shaula core.
- Shell artifact guard precondition runs before capture and may fail deterministically with `ERR_CAPTURE_PRECONDITION_TIMEOUT` when constraints are not met within timeout.

## Future Feature Matrix

| Feature | Phasing | Feasibility | Dependencies | Risk | Go/No-Go Metric |
| :--- | :--- | :--- | :--- | :--- | :--- |
| All-in-one Overlay | Phase 2 | High | `layer-shell` v1 | High UX complexity | User can switch modes (area/win/full) in < 100ms. |
| Pre-area Flow | Phase 2 | Medium | Overlay logic | Latency in startup | Initial overlay paint p95 <= 75ms. |
| Scrolling Capture | Experimental | Low | Compositor-specific IPC | High uncertainty/fragility | Successful stitch of 3+ viewports without artifacts. |
| Self-timer | Phase 2 | High | Daemon clock | Minimal | Precision within +/- 50ms of target delay. |
| OCR Integration | Phase 3 | Medium | Tesseract/Off-process | CPU/Memory pressure | < 5% CPU impact on daemon during background OCR. |
| Screen Recording | Phase 4 | Medium | `dmabuf`, `pipewire` | Resource intensive | Zero frames dropped during 60s 1080p recording. |
| Pin-to-screen | Phase 3 | Medium | `layer-shell` (top) | Window management conflicts | Pin persists across workspace switches (if supported). |
| Hide Desktop Icons | Phase 2 | Low | Desktop Portal / Script | DE-specific fragility | Icons hidden and restored in 100% of test runs. |
| Open from Clipboard | Phase 2 | High | `wl-clipboard` | Data format mismatch | Valid image import in < 200ms. |
| Presets/Aspect Ratio| Phase 2 | High | Overlay constraints | Minimal | Constraint enforcement within +/- 1px accuracy. |
| Advanced History | Phase 3 | Medium | SQLite / Metadata | Database bloat | Search query response for 1000+ entries < 50ms. |

## Scrolling Capture: Experimental
Scrolling capture remains the most uncertain feature due to the lack of standardized Wayland protocols for "requesting scroll" from a client.
- **Experimental Status**: No commitment to MVP or Phase 2.
- **Dependencies**: Requires stable IPC hooks for scrolling in Niri or high-level xdg-portal support.
- **Go/No-Go**: Experimental until a deterministic viewport-stitching algorithm is validated.
