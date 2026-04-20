# F3 Real Manual QA — Shaula

Date (UTC): 2026-04-20
Scope: Real hands-on QA for user-facing CLI flows and overlay/precondition behavior

## Flows Executed (exact commands)

```bash
zig build
./zig-out/bin/shaula capture area --dry-run --json
./zig-out/bin/shaula capture area --dry-run --json --simulate-cancel
./zig-out/bin/shaula capabilities list --json
./zig-out/bin/shaula history list --json
./zig-out/bin/shaula history show --json --id latest
./zig-out/bin/shaula history show --json --id does-not-exist
SHAULA_SOCKET="/tmp/shaula-f3-manual-daemon.sock" ./zig-out/bin/shaula daemon status --json
rm -f "/tmp/shaula-f3-manual-daemon.sock"
SHAULA_SOCKET="/tmp/shaula-f3-manual-daemon.sock" ./zig-out/bin/shaula daemon start --json
SHAULA_SOCKET="/tmp/shaula-f3-manual-daemon.sock" ./zig-out/bin/shaula daemon status --json
SHAULA_SOCKET="/tmp/shaula-f3-manual-daemon.sock" ./zig-out/bin/shaula daemon stop --json
bash ./scripts/qa/assert-overlay-base-selection.sh
bash ./scripts/qa/test-overlay-cancel.sh
bash ./scripts/qa/assert-daemon-status-ipc-truth.sh
bash ./scripts/qa/assert-shell-artifact-guard.sh --inject-known-marker
bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh
bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin
```

## Observed Results

### 1) capture area --dry-run (happy + cancel)
- `capture area --dry-run --json` -> rc=0, `.ok=true`, `.selection.cancelled=false`, geometry present (`x/y/width/height`) and positive sizes.
- `capture area --dry-run --json --simulate-cancel` -> rc=33, `.ok=false`, `.error.code="ERR_SELECTION_CANCELLED"`, deterministic envelope.
- Evidence files:
  - `.sisyphus/evidence/task-7-overlay-base.txt`
  - `.sisyphus/evidence/task-7-overlay-base-error.txt`

### 2) history commands (happy + failure)
- `history list --json` -> rc=0, `.ok=true`, `.result.entries` array populated (Top-N retained entries observed).
- `history show --json --id latest` -> rc=0, `.ok=true`, `.result.entry` contains `path/mime/dimensions/backend_used/timestamp`.
- `history show --json --id does-not-exist` -> rc=52, `.ok=false`, `.error.code="ERR_HISTORY_ENTRY_NOT_FOUND"`.

### 3) capabilities list
- `capabilities list --json` -> rc=0, `.ok=true`, capture capabilities reported (`area=true`, `fullscreen=true`, `window=false`), backend `niri-wayland-direct`.

### 4) daemon status contract (failure + success)
- Failure path: `SHAULA_SOCKET=/tmp/shaula-f3-manual-daemon.sock daemon status --json` (without daemon) -> rc=21, `.ok=false`, `.error.code="ERR_DAEMON_NOT_RUNNING"`.
- Success path:
  - `daemon start --json` -> rc=0, ready state.
  - `daemon status --json` -> rc=0, `.ok=true`, `state/result.state="ready"`, `result.ipc_version="1.0.0"`.
  - `daemon stop --json` -> rc=0.
- IPC truth + orphan negative path script passed:
  - `bash ./scripts/qa/assert-daemon-status-ipc-truth.sh` -> PASS
  - Evidence:
    - `.sisyphus/evidence/task-4-daemon-status-truth.txt`
    - `.sisyphus/evidence/task-4-daemon-status-truth-error.txt` (orphan socket produced deterministic error envelope; observed `ERR_IPC_TIMEOUT`)

### 5) overlay-related + shell artifact guard + timeout determinism
- `bash ./scripts/qa/assert-overlay-base-selection.sh` -> PASS (valid geometry, non-cancelled dry-run)
- `bash ./scripts/qa/test-overlay-cancel.sh` -> PASS (`ERR_SELECTION_CANCELLED` expected)
- `bash ./scripts/qa/assert-shell-artifact-guard.sh --inject-known-marker` -> PASS
  - Evidence `.sisyphus/evidence/task-8-shell-artifact-guard.json` shows:
    - handshake warning present: `capture_precondition_panel_hidden_handshake`
    - panel marker absent in capture output (`panel_marker_absent.pass=true`)
- `bash ./scripts/qa/assert-noctalia-capture-with-panel-hide.sh` -> PASS
  - Evidence `.sisyphus/evidence/task-8-shell-artifact-guard-error.txt` confirms timeout path deterministic:
    - `.error.code="ERR_CAPTURE_PRECONDITION_TIMEOUT"`
    - warning contains `capture_precondition_guard_timeout`

### 6) Noctalia optionality (core works without plugin)
- `bash ./scripts/qa/test-noctalia-plugin-optional.sh --without-plugin` -> PASS
- Existing evidence `.sisyphus/evidence/task-9-noctalia-actions.json` indicates `"plugin_optional": true` and adapter checks pass.
- Combined result: core capture flows remain operational without plugin dependency.

## Failures
- No unexpected failures.
- Expected negative-path failures observed and deterministic:
  - `ERR_SELECTION_CANCELLED`
  - `ERR_HISTORY_ENTRY_NOT_FOUND`
  - `ERR_DAEMON_NOT_RUNNING` / `ERR_IPC_TIMEOUT`
  - `ERR_CAPTURE_PRECONDITION_TIMEOUT`

## Verdict

**APPROVE**

The required manual QA scope is satisfied with reproducible command evidence for happy and failure paths across capture dry-run/cancel, history commands, daemon status contracts, overlay behavior, shell artifact guard checks, timeout determinism, and Noctalia optionality.
