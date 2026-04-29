# Shaula Delivery Focus

See [spec/algo.md](algo.md) for the central technical blueprint and performance targets.

This document tracks current delivery lanes and candidate product phases for Shaula. Candidate phases are not supported public contract until they are implemented, capability-gated, and covered by deterministic `ERR_*` behavior.

## Track 1: Contract Stability

Focus:

- keep the CLI grammar stable,
- keep JSON envelopes deterministic,
- preserve `ERR_*` to exit-code mapping.

Go/No-Go:

- `zig build`
- `zig build test`
- `bash scripts/qa/run-integration-tests.sh`

## Track 2: Niri-First Capture Flow

Focus:

- fast area selection,
- explicit confirm/cancel flow,
- aspect-constrained selection,
- helper-first overlay with honest fallback behavior.
- Shottr-level interaction quality for precision, latency, and confidence.

Go/No-Go:

- `bash scripts/qa/assert-overlay-helper-interactive.sh`
- `bash scripts/qa/run-performance-gates.sh`

## Track 3: v0 Solid Capture Base

Focus:

- fullscreen capture,
- region capture,
- focused output capture,
- focused window capture via Niri IPC,
- save to file and copy image to clipboard,
- TOML configuration,
- polished overlay basics: Esc cancel, Enter confirm, visible size,
- repeat last confirmed region.

Go/No-Go:

- `./dev check`
- `./dev doctor`
- `./dev strategies`

## Track 4: Low-Hanging UX Polish

Focus:

- clearer size and coordinate feedback,
- stronger handle affordances for move/resize,
- stable toolbar placement and anti-jitter behavior,
- keyboard polish: Esc, Enter, nudging, and focus predictability,
- previous-area and toolbar persistence reliability,
- output polish: filename templates, copy-path, and post-capture action defaults,
- small utilities that do not require a new UI shell, such as color picker or manual measurement.

Go/No-Go:

- no replacement of the current overlay architecture,
- no full editor dependency,
- no capture hot-path regression,
- all new failures map to deterministic `ERR_*` outcomes.

## Track 5: v1 Differentiating UX

Focus:

- floating post-capture preview,
- basic editor with crop,
- pixelate/redaction,
- arrows and rectangles,
- pin screenshot,
- color picker,
- magnifier,
- ruler/manual measurement,
- simple capture history,
- configurable file naming.

Go/No-Go:

- hot-path latency remains within `spec/performance.md`,
- optional preview/editor/pin failures remain outside capture-critical completion,
- all new public failures map to deterministic `ERR_*` outcomes.

## Track 6: v2 Power User Surface

Focus:

- region OCR,
- QR reader,
- pretty export with padding, shadow, and rounded corners,
- combine screenshots,
- S3-compatible upload,
- smart selection,
- remove object,
- scrolling screenshot after a clear Wayland/Niri strategy exists.

Go/No-Go:

- external engines, uploads, and image-editing workers are optional and non-blocking,
- capability absence is represented explicitly instead of placeholder UI,
- contracts are versioned before automation-visible shape changes.

## Track 7: Release Hardening

Focus:

- release-readiness evidence,
- e2e stability on Niri,
- non-blocking optional integrations such as Noctalia.

Go/No-Go:

- `bash scripts/qa/run-e2e-niri.sh`
- `bash scripts/qa/release-readiness-capture-fix.sh`
- `bash scripts/qa/run-all-tests.sh`
