# Shaula Testing Strategy (Task 17)

See [spec/algo.md](algo.md) for the locked engineering decisions and [spec/performance.md](performance.md) for the budgets the tests protect.

## Scope

This document defines the deterministic, machine-first QA strategy for Shaula.
The operational script classification lives in
[`scripts/qa/README.md`](../scripts/qa/README.md); that file is the source of
truth for which wrappers are current gates versus manual/legacy investigation.

Historic QA scripts still exist across three layers:

1. Unit/contracts/mocks (no real compositor required)
2. Integration harness (local deterministic flows)
3. E2E on real Niri session (strictly gated)

The matrix enforces final post-fix contracts for:

- runtime capture backend execution without productive stub success path
- capabilities strict contract between `capabilities list` and `capture <mode>`
- saved default output path `~/Pictures/shaula` with deterministic invalid-path failure
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

### Current Gate (`./dev check`, `git diff --check`, `./dev qa`)

- `./dev check` builds and runs Zig/C helper tests.
- `git diff --check` rejects whitespace errors.
- `./dev qa` runs the curated non-intrusive contract lane:
  `run-all-tests.sh` -> `run-unit-tests.sh` -> preflight schema, failure
  matrix, and exit-code mapping.

### Unit / Contracts / Mocks (`scripts/qa/run-unit-tests.sh`)

- Preflight and capabilities envelope schema checks
- Error taxonomy and exit-code mapping checks
- Deterministic failure matrix without real compositor dependency
- Runtime backend unavailability deterministic failure mapping checks
- Pre-capture guard timeout and warning-token contract checks

### Manual / Legacy Integration (`scripts/qa/run-integration-tests.sh`)

This wrapper remains useful for investigation but is not a default release gate.
It aggregates older task evidence and may include stale runtime assumptions
until individual subchecks are refreshed.

- Daemon lifecycle and state machine checks
- Capture core modes (`area`, `fullscreen`) with decoded PNG integrity checks
- Post-capture pipeline (`--save`, `--copy`, partial behavior)
- Capabilities strict contract parity checks (`capabilities.capture.*` vs execution outcomes)
- Default saved-output path checks for `~/Pictures/shaula`
- History Top-N 20 consistency checks
- Overlay base selection checks and deterministic cancel path checks
- Shell artifact guard checks for handshake and bounded settle fallback behavior
- Optional Noctalia adapter checks (action parity, plugin optionality, overhead budget)

### Manual / Legacy E2E Real Niri (`scripts/qa/run-e2e-niri.sh`)

This wrapper requires careful interpretation. It may degrade missing real Niri
preflight in headless environments, and it is not a substitute for targeted
manual checks after overlay/capture/Wayland changes.

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

The current `./dev qa` report only contains the curated unit/contract layer.
Older integration/e2e/performance reports are investigation artifacts unless a
release task explicitly refreshes and promotes them.

## Non-Interactive CI Rules

- All scripts use `set -euo pipefail`.
- Expected-failure paths are captured with `set +e` wrappers only where required.
- Scripts emit deterministic `ERR_*` tokens on hard failures.
- No manual acceptance steps are required.
