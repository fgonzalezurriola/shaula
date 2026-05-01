# Shaula Testing Strategy (Task 17)

See [spec/algo.md](algo.md) for the locked engineering decisions and [spec/performance.md](performance.md) for the budgets the tests protect.

## Scope

This document defines the deterministic, machine-first QA strategy for Shaula across three layers:

1. Unit/contracts/mocks (no real compositor required)
2. Integration harness (local deterministic flows)
3. E2E on real Niri session (strictly gated)

The matrix enforces final post-fix contracts for:

- runtime capture backend execution without productive stub success path
- capabilities strict contract between `capabilities list` and `capture <mode>`
- default output path `~/Pictures/Shaula` with deterministic invalid-path failure
- history retention Top-N 20 newest-first
- overlay v1 base selection and cancellation contract
- shell artifact pre-capture guard with bounded timeout behavior
- optional Noctalia MVP adapter integration without hot-path coupling

## Preflight QA Gate

E2E execution is blocked unless the environment is explicitly ready for Niri/Wayland.

Hard requirements:

- `WAYLAND_DISPLAY` must be set.
- `NIRI_SOCKET` must be set and point to a Unix socket.
- `shaula preflight --json` under `SHAULA_COMPOSITOR=niri` must return `ok=true`.
- `result.wayland=true` and `result.ipc_ready=true` are mandatory before E2E.

Deterministic failure token for non-ready environment:

- `ERR_PREFLIGHT_ENV_NOT_READY`

## Test Matrix

### Unit / Contracts / Mocks (`scripts/qa/run-unit-tests.sh`)

- Preflight and capabilities envelope schema checks
- Error taxonomy and exit-code mapping checks
- Deterministic failure matrix without real compositor dependency
- Runtime backend unavailability deterministic failure mapping checks
- Pre-capture guard timeout and warning-token contract checks

### Integration (`scripts/qa/run-integration-tests.sh`)

- Daemon lifecycle and state machine checks
- Capture core modes (`area`, `fullscreen`) with decoded PNG integrity checks
- Post-capture pipeline (`--save`, `--copy`, partial behavior)
- Capabilities strict contract parity checks (`capabilities.capture.*` vs execution outcomes)
- Default output path checks for `~/Pictures/Shaula`
- History Top-N 20 consistency checks
- Overlay base selection checks and deterministic cancel path checks
- Shell artifact guard checks for handshake and bounded settle fallback behavior
- Optional Noctalia adapter checks (action parity, plugin optionality, overhead budget)

### E2E Real Niri (`scripts/qa/run-e2e-niri.sh`)

- Strict `Preflight QA Gate` before any capture test
- Capture flows: `area`, `fullscreen`, `window`
- Clipboard path behavior checks
- Compositor failure path (`ERR_UNSUPPORTED_COMPOSITOR`)
- Permission/degraded path (`ERR_CLIPBOARD_UNAVAILABLE` under forced unavailable env)
- Daemon/backend state path (`start/status/stop` + capabilities backend keys)
- Strict capability parity and pre-capture guard behavior included in E2E layer report
- Optional Noctalia path checks must not affect capture hot-path pass criteria

## Evidence and Output Contract

- Primary report: `.qa/evidence/task-16-full-regression.json`
- Negative preflight evidence: `.qa/evidence/task-16-full-regression-error.txt`

Report format is stable and machine-checkable:

- `suite`
- `timestamp`
- `pass`
- `layers` (`unit`, `integration`, `e2e_niri`)
- `matrix` with explicit case IDs and pass/fail fields
- `subchecks` for runtime integrity, strict capabilities, default output, history Top-N, overlay base, precondition guard, and optional Noctalia integration

## Non-Interactive CI Rules

- All scripts use `set -euo pipefail`.
- Expected-failure paths are captured with `set +e` wrappers only where required.
- Scripts emit deterministic `ERR_*` tokens on hard failures.
- No manual acceptance steps are required.
