# Shaula Performance Spec

This document defines the hard performance budgets and resource constraints for Shaula v1, focusing on Niri-first hot-path isolation.

## Performance Budgets (p95)

## Latency SLO

All measurements are targetted at p95 latency under normal system load.

| Operation | Budget (p95) | Budget (p99) | Rationale |
| :--- | :--- | :--- | :--- |
| Overlay Selection to First Paint | <= 75ms | <= 110ms | "Instant" feel for user interaction. |
| Area/Fullscreen Capture Completion | <= 150ms | <= 200ms | Fast feedback loop for quick captures. |
| Window Capture Completion | <= 220ms | <= 300ms | Account for window/tile identity resolution. |
| Daemon Idle CPU Usage | <= 0.5% | <= 1.0% | Zero impact when not active. |
| Daemon Idle Memory (RSS) | <= 40MB | <= 60MB | Minimal footprint for a background service. |

## Resource Constraints

- **Single-Threaded Hot Path**: Capture operations must remain single-threaded or use non-blocking asynchronous I/O to avoid context-switching overhead.
- **Hot-Path Isolation**: Plugins, history persistence, and any heavy post-capture tasks must run outside the capture critical path or after the artifact is ready.
- **Zero-Copy Capture**: Preference for DMA-BUF or shared memory protocols where supported by Niri to minimize pixel copying.

## Validation Gates

Performance budgets are enforced via the following QA scripts:
- `scripts/qa/benchmark-overlay-first-paint.sh`
- `scripts/qa/benchmark-capture-completion.sh`
- `scripts/qa/benchmark-daemon-idle.sh`

Failure to meet these budgets results in a build/deployment block under the `ERR_PERFORMANCE_GATE_FAILURE` token.
