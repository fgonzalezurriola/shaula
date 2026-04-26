# Shaula Delivery Focus

See [spec/algo.md](algo.md) for the central technical blueprint and performance targets.

This document tracks the current delivery lanes for Shaula instead of speculative future phases.

## Track 1: Contract Stability

Focus:

- keep the CLI grammar stable,
- keep JSON envelopes deterministic,
- preserve `ERR_*` to exit-code mapping.

Go/No-Go:

- `zig build`
- `zig build test`
- `bash scripts/qa/run-integration-tests.sh`

## Track 2: CleanShot-Like Capture Flow

Focus:

- fast area selection,
- explicit confirm/cancel flow,
- aspect-constrained selection,
- helper-first overlay with honest fallback behavior.

Go/No-Go:

- `bash scripts/qa/assert-overlay-helper-interactive.sh`
- `bash scripts/qa/run-performance-gates.sh`

## Track 3: Release Hardening

Focus:

- release-readiness evidence,
- e2e stability on Niri,
- non-blocking optional integrations such as Noctalia.

Go/No-Go:

- `bash scripts/qa/run-e2e-niri.sh`
- `bash scripts/qa/release-readiness-capture-fix.sh`
- `bash scripts/qa/run-all-tests.sh`
