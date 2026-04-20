## 2026-04-18T20:10:24Z Task: session-bootstrap
- Active plan: shaula-niri-wayland-instant-capture.
- Execution starts from Task 1 and proceeds following dependency matrix and wave structure.

## 2026-04-19T04:02:34Z Task: task-15-noctalia-plugin
- Keep Noctalia integration out of capture hot path by implementing plugin PoC as standalone adapter scripts under `integrations/noctalia/*` and invoking daemon through versioned IPC only.
- Enforce optionality contract in QA: plugin-present path validates daemon-triggered flow; plugin-absent path validates unchanged core capture behavior.
- Enforce measurable non-critical overhead budget with p95 added latency benchmark gate (`<=15ms`) and deterministic failure tokening.
### Feature Phasing Strategy
- Phase 2 focuses on interactivity (overlay enhancements, self-timer).
- Phase 3 moves heavy processing (OCR) and storage (history) to background workers.
- Phase 4 targets multimedia (recording) and experimental protocols (scrolling).
- Deterministic error token `ERR_FUTURE_FEATURE_GATE_MISSING` enforced for specs lacking measurable metrics.

## 2026-04-19T04:22:42Z Task: task-17-test-matrix
- Chosen testing architecture is layered and deterministic: unit/contracts first, integration harness second, and Niri-real E2E last under strict preflight gating.
- Negative preflight evidence is generated inside `run-all-tests.sh` by forcing an env-not-ready execution path (unset `WAYLAND_DISPLAY` and `NIRI_SOCKET`) to guarantee deterministic `ERR_PREFLIGHT_ENV_NOT_READY` token capture.
- The main report format is fixed to stable keys (`suite`, `timestamp`, `pass`, `layers`, `matrix`) so downstream automation can parse outcomes without heuristic text processing.
### Task 18 Decisions
- **Executive Technical Summary**: Added to 'algo.md' to provide immediate context for the entire project.
- **Uncertainty Register**: Formalized in 'algo.md' to track known unknowns without blocking MVP.
- **Risk/Dependency Matrix**: Integrated into 'algo.md' for better visibility of project constraints.
- **Performance Spec**: Created 'performance.md' as a standalone source of truth for budgets.

## 2026-04-19T05:27:47Z Task: final-wave-blocker-alignment
- Chose strict grammar enforcement in runtime (require explicit `capabilities list`) rather than broad backward-compat aliases, because Final Wave blocker scope demanded doc/runtime alignment and deterministic contracts with minimal refactor risk.
- Standardized history outputs to AGENT-FIRST envelope (`ok`, `contract_version`, `command`, `timestamp`, `result`, `warnings`) and moved list payload into `result.entries` to keep machine parsing uniform across list/show commands.
- Implemented `history show` with explicit `latest` id support as minimal safe behavior in scope; non-`latest` ids fail deterministically with `ERR_HISTORY_ENTRY_NOT_FOUND` until multi-entry persistence is introduced in a separate scoped task.
