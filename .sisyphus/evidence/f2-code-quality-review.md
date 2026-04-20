# F2 Code Quality Review — unspecified-high (rerun)

Date: 2026-04-20T00:58:04Z
Plan: `.sisyphus/plans/shaula-capture-integrity-noctalia-overlay.md`

## Files Reviewed

- `src/capture/command.zig`
- `scripts/qa/assert-no-runtime-stub.sh`
- `spec/architecture.md`
- `spec/requirements.md`
- Prior F2 report baseline: `.sisyphus/evidence/f2-code-quality-review.md` (previous REJECT content)

## Positive Findings

1. **Non-dry-run area capture now executes overlay selection flow before backend capture**.
   - `src/capture/command.zig:71-80` runs `overlay.runSelection(...)` unconditionally for area mode (with controlled dry-run fallback only for non-interactive environments).
   - `src/capture/command.zig:81-84` now returns deterministic `ERR_SELECTION_CANCELLED` before backend execution when selection is cancelled.

2. **Forced-stub taxonomy is now deterministic and aligned to spec contract**.
   - `src/capture/command.zig:163-180` handles `runtime.backend == .stub` first and emits `ERR_CAPTURE_BACKEND_UNAVAILABLE` (retryable) with backend label `__stub__`.
   - This aligns with locked requirements in:
     - `spec/architecture.md:172`
     - `spec/requirements.md:22`

3. **QA gate now enforces one canonical forced-stub error code**.
   - `scripts/qa/assert-no-runtime-stub.sh:59-63` now requires only `.error.code == "ERR_CAPTURE_BACKEND_UNAVAILABLE"` and `.backend_used == "__stub__"`.

## Risks

- No blocking risks found in the two previously rejected blocker areas.
- Existing non-blocking nuance remains documented: non-interactive environments use deterministic overlay dry-run fallback to avoid CI hangs while preserving selection/cancel contracts.

## Required Fixes (if any)

- None for F2 blocker scope.

## Command Evidence (concise)

1. Non-dry-run cancel path:
   - Command: `./zig-out/bin/shaula capture area --json --simulate-cancel`
   - Observed: `rc=33`, `error.code="ERR_SELECTION_CANCELLED"`

2. Forced stub path:
   - Command: `SHAULA_CAPTURE_BACKEND=__stub__ SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula capture area --json --output /tmp/shaula-f2-rerun-stub.png`
   - Observed: `rc=30`, `error.code="ERR_CAPTURE_BACKEND_UNAVAILABLE"`, `backend_used="__stub__"`

## Verdict

**APPROVE**

Rationale: Both previously blocking issues are resolved in current code state and aligned with locked spec contracts. The non-dry-run area flow now includes overlay selection + deterministic cancel handling, and forced-stub taxonomy is canonical (`ERR_CAPTURE_BACKEND_UNAVAILABLE`) in both implementation and QA enforcement.
