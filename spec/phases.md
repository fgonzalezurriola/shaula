# Shaula Phasing and Go/No-Go Criteria

See [spec/algo.md](algo.md) for the central technical blueprint and performance targets.

## Phase 0: Foundations and Contract Lock

Focus: lock AGENT-FIRST contract grammar, deterministic envelopes, and Niri-first capability boundaries before feature expansion.
- **Go/No-Go**: `validate-cli-contract.sh` passes and runtime command grammar matches documented commands.

## Phase 1: MVP Core

Focus: Fast, deterministic, agent-first area, fullscreen, and window capture.
- **Go/No-Go**: All core capture commands pass `scripts/qa/validate-all-modes.sh` with p95 <= 220ms.

## Phase 2: Enhanced Interactivity
Features: All-in-one Overlay, Pre-area Flow, Self-timer, Hide Desktop Icons, Open from Clipboard, Presets/Aspect Ratio.
- **Go/No-Go**: Overlay interactivity must not degrade capture hot-path latency beyond the 110ms p99 overlay-to-paint budget.

## Phase 3: Advanced Post-processing and Storage
Features: OCR Integration, Pin-to-screen, Advanced History.
- **Go/No-Go**: OCR background workers must not exceed 40MB additional RSS on the daemon.

## Phase 4: Multimedia and Specialized Flow
Features: Screen Recording, Scrolling Capture (if promoted from experimental).
- **Go/No-Go**: Recording stability must reach 100% success rate on 1080p60 for >= 60 seconds without process crashes.

## Hardening

Cross-phase hardening ensures deterministic `ERR_*` failures, CI-safe QA scripts, and strict machine-first output parity between docs and runtime.

## Noctalia

Noctalia integration stays optional, post-MVP, and non-blocking for all core capture paths.

## Go/No-Go Detailed Criteria for Future Feature Matrix

Each future feature listed in `spec/requirements.md` must meet its specific Go/No-Go metric before promotion to stable/implemented.

### Failure Policy: `ERR_FUTURE_FEATURE_GATE_MISSING`
A feature cannot be scheduled for implementation if it lacks a measurable Go/No-Go metric. The validation script `scripts/qa/validate-future-feature-matrix.sh` enforces this.
