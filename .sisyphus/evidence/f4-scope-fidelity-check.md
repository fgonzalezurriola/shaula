# F4 Scope Fidelity Check — Deep

## Scope Baseline

Plan baseline reviewed from `.sisyphus/plans/shaula-capture-integrity-noctalia-overlay.md`:

- **Objectives/Deliverables**: real runtime capture (no productive stub), strict `capabilities ↔ capture` contract, Top-N=20 history, default output `~/Pictures/Shaula`, overlay v1 base, optional Noctalia MVP, expanded QA evidence (`lines 41-49, 61-67`).
- **Must NOT Have guardrails**: no silent runtime stub fallback, no manual visual checks as close criteria, no overlay scope expansion to advanced editor/annotations, no control-center animation dependence during capture, no non-Niri scope expansion (`lines 68-74`).
- **Final wave target**: this task is F4 scope-fidelity verification (`lines 623-630`).

## Compliance Mapping

### Must Have → Evidence

1. **Real capture fix in production path (no simulated productive output)**
   - Runtime backend executes helper process and fails deterministically for forced stub: `src/backends/capture_backend.zig` (`execute`: `lines 156-290`; stub forced failure `lines 193-204`; runtime helper path `lines 377-427`).
   - QA gate passes: `.sisyphus/evidence/task-10-postfix-test-matrix-report.json` includes `integration.capture.integrity.runtime_non_stub: pass=true` (`lines 33-37`).

2. **Agent-executable QA for real content integrity**
   - Deterministic decoded PNG + stub signature rejection pass: `.sisyphus/evidence/task-3-capture-content-validity.json` (`lines 4-38`).
   - Consolidated in matrix: `integration.capture.integrity.content_validity: pass=true` in Task 10 matrix (`lines 39-43`).

3. **Total consistency between `capabilities` and `capture` behavior**
   - Single runtime source file: `src/capabilities/runtime.zig` (`lines 43-109`).
   - Strict gate reflected in matrix and E2E: `integration.capture.capabilities.strict_contract` pass (`task-10 report lines 45-49`) and `e2e.capture.capabilities.strict_contract` pass (`lines 149-151`).

4. **Top-N=20 history, newest-first deterministic**
   - Evidence confirms `topn=20`, `expected_len=20`, ordered output: `.sisyphus/evidence/task-6-history-topn.txt` (`lines 1-5`).
   - Reinforced in matrix as `integration.history.topn_20: pass=true` (`task-10 report lines 57-61`).

5. **Noctalia optional; core capture works without plugin**
   - Optionality check passes in integration matrix: `integration.noctalia.optional_core_without_plugin: pass=true` (`task-10 report lines 87-91`).
   - QA script explicitly validates core capture path without plugin: `scripts/qa/test-noctalia-plugin-optional.sh` (`lines 91-146`).

### Must NOT Have → Compliance Evidence

1. **No silent runtime stub fallback**
   - Forced `__stub__` fails with deterministic error and no output artifact: `src/backends/capture_backend.zig` (`lines 193-204`, `620-658` tests) and `scripts/qa/assert-no-runtime-stub.sh` (`lines 49-75`).

2. **No manual visual checks for closure**
   - Matrix/readiness are machine-verifiable JSON gates (`.sisyphus/evidence/task-10-postfix-test-matrix-report.json`, `.sisyphus/evidence/task-12-release-readiness.json` lines `4-8, 10-129`), not manual screenshot inspection.

3. **No overlay scope expansion (advanced tooling/editor/annotations)**
   - Overlay implementation remains base selection contract only: `src/overlay/overlay.zig` (`lines 4-12, 24-37, 72-90`).
   - Evidence remains base geometry + cancel flow only: `.sisyphus/evidence/task-7-overlay-base.txt` (`line 1`) and `.sisyphus/evidence/task-7-overlay-base-error.txt` (`line 1`).

4. **No control-center animation dependence for capture correctness**
   - Pre-capture guard enforces handshake/fallback with bounded timeout and deterministic timeout code: `src/capture/precondition_guard.zig` (`lines 20-62`) + evidence `.sisyphus/evidence/task-8-shell-artifact-guard.json` (`lines 4-19`) and timeout path `.sisyphus/evidence/task-8-shell-artifact-guard-error.txt` (`line 1`).

5. **No non-Niri scope expansion in this plan**
   - Non-Niri is explicitly rejected in runtime path (`ERR_UNSUPPORTED_COMPOSITOR`): `src/backends/capture_backend.zig` (`lines 175-187`, test `lines 596-618`).
   - Plan/spec remain Niri-first for v1: `.sisyphus/plans/...` (`line 73`) and `spec/wayland-niri-integration.md` (`lines 5-16`).

## Drift Findings

- **No blocking scope drift found** in implementation/evidence for must-have or guardrail constraints.
- **Minor non-blocking documentation naming debt**: `spec/testing.md` still carries legacy “Task 17” wording/path references (`lines 1, 74`) while primary matrix artifact is Task 10 (`line 73`), already recorded in notepad issues (`.sisyphus/notepads/.../issues.md` lines `52-54`). This is nomenclature drift, not functional scope creep.

## Verdict

**APPROVE**

Delivered work stays within the declared scope and guardrails. Required must-haves are evidenced, must-not-have constraints are respected, and Noctalia remains optional with core capture independence preserved.
