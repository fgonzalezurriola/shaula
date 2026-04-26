# Shaula Raylib + Clay Niri Overlay Integration

## TL;DR
> **Summary**: Integrate a Raylib+Clay helper overlay process for `capture area` that delivers CleanShot-like HUD primitives on Wayland/Niri while preserving existing CLI JSON contracts and deterministic `ERR_*` behavior.
> **Deliverables**:
> - New overlay helper process and IPC/stdio contract
> - `capture area` orchestration with probe/timeout/fallback policy
> - Interactive-overlay QA + performance gates + evidence paths
> - Noctalia-compatible action hooks for open-folder/clipboard-image
> **Effort**: Large
> **Parallel**: YES - 5 waves
> **Critical Path**: Task 1 → Task 2 → Task 4 → Task 7 → Task 10

## Context
### Original Request
- User requested a concrete implementation plan for Raylib+Clay integration and a CleanShot-like overlay outcome on Wayland/Niri.

### Interview Summary
- Current overlay UX exists and works via `slurp`, but user wants a richer, instant-feeling HUD.
- User previously observed GTK MVP startup around ~200ms and prefers lower perceived latency.
- Existing repo already has strict CLI contracts and deterministic QA gates that must remain stable.

### Metis Review (gaps addressed)
- Added explicit feasibility spike and go/no-go criteria before full rollout.
- Added deterministic failure taxonomy/fallback policy (no hidden behavior).
- Added mandatory interactive (non-dry-run) perf/QA gates to avoid false confidence.
- Added multi-output/scaling and cancel/timeout edge-case coverage.

## Work Objectives
### Core Objective
Ship a Raylib+Clay overlay MVP for `capture area` on Niri with CleanShot-like selection HUD elements, preserving all existing command-family JSON contracts and deterministic error semantics.

### Deliverables
- Overlay helper executable/subcommand implementing:
  - dimmed backdrop
  - freeform selection rectangle
  - live size readout (w × h)
  - aspect-ratio dropdown (MVP list)
  - minimal tool strip (Area, Fullscreen, Window, Timer placeholder, OCR placeholder, Record placeholder as disabled badges)
  - confirm/cancel actions
- Parent capture orchestration using helper with timeout/fallback.
- New explicit overlay failure taxonomy and mapping.
- QA scripts and evidence reports for interactive overlay path and perf budgets.

### Definition of Done (verifiable conditions with commands)
- `zig build` passes.
- `zig build test` passes.
- `bash scripts/qa/run-integration-tests.sh` passes.
- `bash scripts/qa/run-e2e-niri.sh` passes.
- `bash scripts/qa/run-performance-gates.sh` passes with new interactive-overlay first-paint gate.
- `./zig-out/bin/shaula capture area --json` returns unchanged envelope keys with valid selection/capture path.
- `set +e; ./zig-out/bin/shaula capture area --json --simulate-cancel >/tmp/cancel.json 2>&1; rc=$?; set -e; test "$rc" -eq 33` and `jq -e '.ok==false and .error.code=="ERR_SELECTION_CANCELLED"' /tmp/cancel.json` passes.

### Must Have
- Keep existing command-family output schema (`capture`, `history`, `daemon`, `capabilities`) unchanged.
- Deterministic `ERR_*` mapping for all new overlay failure modes.
- Runtime fallback strategy when overlay helper cannot initialize on Niri/Wayland.
- Non-interactive path retained for CI and headless contexts.
- Agent-executable QA only (no manual visual gates).

### Must NOT Have (guardrails, AI slop patterns, scope boundaries)
- No full editor/annotation suite in this scope.
- No hidden dependency on Noctalia for core capture.
- No silent contract drift in JSON fields or exit codes.
- No replacing fullscreen/window backend behavior in this iteration.
- No disabling existing integration/e2e gates to “pass” rollout.

## Verification Strategy
> ZERO HUMAN INTERVENTION - all verification is agent-executed.
- Test decision: tests-after + existing Zig test framework + Bash QA scripts
- QA policy: Every task includes explicit happy + failure scenario commands
- Evidence: `.sisyphus/evidence/task-{N}-{slug}.{ext}`

## Execution Strategy
### Parallel Execution Waves
> Target: 5-8 tasks per wave.

Wave 1: Feasibility + contracts + build plumbing
- Task 1 (architecture spike), Task 2 (overlay stdio contract), Task 3 (build/dependency wiring), Task 4 (taxonomy additions)

Wave 2: Helper UI MVP primitives
- Task 5 (Raylib window/bootstrap), Task 6 (Clay layout/HUD), Task 7 (selection state engine)

Wave 3: Parent orchestration and fallback integration
- Task 8 (overlay runner integration in `overlay.zig`), Task 9 (capture command orchestration updates), Task 10 (Noctalia action hooks)

Wave 4: QA/perf hardening
- Task 11 (interactive QA suite), Task 12 (perf gates), Task 13 (multi-output/scaling fixtures)

Wave 5: Docs and release closure
- Task 14 (DEV/architecture/testing docs), Task 15 (release readiness updates), Task 16 (final regression sweep)

### Dependency Matrix (full, all tasks)
- 1 blocks 5,6,7,8
- 2 blocks 8,9,11
- 3 blocks 5,6
- 4 blocks 9,11
- 5 blocks 6,7
- 6 blocks 7
- 7 blocks 8,9
- 8 blocks 9,11,12
- 9 blocks 11,12,15
- 10 blocks 15
- 11 blocks 16
- 12 blocks 16
- 13 blocks 16
- 14 blocks 15
- 15 blocks 16
- 16 blocks Final Verification Wave

### Agent Dispatch Summary (wave → task count → categories)
- Wave 1 → 4 tasks → deep/unspecified-high
- Wave 2 → 3 tasks → visual-engineering + deep
- Wave 3 → 3 tasks → deep/unspecified-high
- Wave 4 → 3 tasks → deep/unspecified-high
- Wave 5 → 3 tasks → writing/deep

## TODOs
> Implementation + Test = ONE task.

- [x] 1. Raylib+Clay feasibility spike on Niri

  **What to do**:
  - Implement a throwaway overlay helper spike proving: create window, draw dim layer, drag selection, Esc cancel, Enter confirm.
  - Measure cold start and first interactive frame timestamps in-process.
  - Emit a one-line JSON result for success/cancel/error.
  **Must NOT do**:
  - Do not modify existing `capture area` production path yet.
  - Do not introduce schema changes in parent command outputs.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: resolves go/no-go architecture risk early.
  - Skills: `[]` - no special skill required.
  - Omitted: `review-work` - not needed during spike.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 5,6,7,8 | Blocked By: none

  **References**:
  - Pattern: `src/overlay/overlay.zig` - current selection contract.
  - Pattern: `src/selection/selection.zig` - geometry/result structs.
  - API/Type: `src/capture/command.zig:runArea` - parent orchestrator constraints.
  - Test: `scripts/qa/benchmark-overlay-first-paint.sh` - latency evidence pattern.
  - External: `https://github.com/raysan5/raylib/issues/4371` - Wayland caveats.
  - External: `https://github.com/nicbarker/clay/blob/main/renderers/raylib/clay_renderer_raylib.c` - renderer model.

  **Acceptance Criteria**:
  - [ ] `zig build` succeeds with spike code behind feature flag.
  - [ ] Evidence JSON `.sisyphus/evidence/task-1-raylib-clay-feasibility.json` contains `{"viable":true|false,"startup_ms":N,"first_frame_ms":N}`.

  **QA Scenarios**:
  ```
  Scenario: Happy path viability probe
    Tool: Bash
    Steps: Build spike binary and run probe command with Niri env.
    Expected: Exit 0; evidence file reports viable=true and measured timings.
    Evidence: .sisyphus/evidence/task-1-raylib-clay-feasibility.json

  Scenario: Failure path when display/protocol unavailable
    Tool: Bash
    Steps: Unset WAYLAND_DISPLAY/NIRI_SOCKET and run probe.
    Expected: Deterministic non-zero with `ERR_OVERLAY_UNAVAILABLE` in output.
    Evidence: .sisyphus/evidence/task-1-raylib-clay-feasibility-error.txt
  ```

  **Commit**: YES | Message: `feat(overlay): spike raylib clay viability on niri` | Files: [new overlay spike files, build wiring]

- [x] 2. Define overlay helper stdio contract v1

  **What to do**:
  - Specify and implement helper output envelope, e.g. `{status:"ok|cancel|error", geometry:{x,y,width,height}, action:"capture|cancel", error:{code,message}}`.
  - Parent parser in `overlay.zig` must map helper responses to `SelectionResult` deterministically.
  **Must NOT do**:
  - Do not change public `capture area` JSON envelope.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: contract-critical boundary.
  - Skills: `[]`
  - Omitted: `visual-engineering` - contract work first.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 8,9,11 | Blocked By: none

  **References**:
  - Pattern: `src/overlay/overlay.zig` - current parsing path from `slurp` stdout.
  - API/Type: `src/selection/selection.zig:SelectionResult`.
  - Test: `scripts/qa/assert-overlay-base-selection.sh`.

  **Acceptance Criteria**:
  - [ ] Helper contract document added under `spec/wayland-niri-integration.md` section.
  - [ ] Parser unit tests cover `ok`, `cancel`, malformed payload.

  **QA Scenarios**:
  ```
  Scenario: Helper returns valid geometry
    Tool: Bash
    Steps: Run helper in deterministic test mode emitting fixed geometry JSON.
    Expected: Parent maps to SelectionResult.cancelled=false with exact geometry.
    Evidence: .sisyphus/evidence/task-2-overlay-contract-ok.json

  Scenario: Helper returns malformed json
    Tool: Bash
    Steps: Inject invalid payload from helper test mode.
    Expected: Parent returns cancelled=true and deterministic error token path.
    Evidence: .sisyphus/evidence/task-2-overlay-contract-error.txt
  ```

  **Commit**: YES | Message: `feat(overlay): add helper stdio contract v1` | Files: [overlay parser, spec updates, tests]

- [x] 3. Add Raylib+Clay dependency wiring in build/test

  **What to do**:
  - Update `build.zig` and dependency resolution to include Raylib and Clay integration.
  - Ensure test compilation remains deterministic via `src/test_root.zig` imports.
  **Must NOT do**:
  - Do not break existing non-UI test execution.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: build graph correctness.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 5,6 | Blocked By: none

  **References**:
  - Pattern: `build.zig` executable/test roots.
  - Pattern: `src/test_root.zig` manual registry.

  **Acceptance Criteria**:
  - [ ] `zig build` succeeds with Raylib+Clay linked.
  - [ ] `zig build test` succeeds with overlay tests included.

  **QA Scenarios**:
  ```
  Scenario: Build success
    Tool: Bash
    Steps: Run `zig build`.
    Expected: Exit 0.
    Evidence: .sisyphus/evidence/task-3-build-success.txt

  Scenario: Dependency missing path
    Tool: Bash
    Steps: Run build in env simulating missing raylib.
    Expected: Deterministic failure message with actionable token.
    Evidence: .sisyphus/evidence/task-3-build-missing-deps.txt
  ```

  **Commit**: YES | Message: `build(ui): wire raylib clay dependencies` | Files: [build.zig, test_root.zig, lockfiles]

- [x] 4. Extend deterministic error taxonomy for overlay helper failures

  **What to do**:
  - Add explicit codes: `ERR_OVERLAY_UNAVAILABLE`, `ERR_OVERLAY_TIMEOUT`, `ERR_OVERLAY_PROTOCOL_INVALID` (or equivalent naming aligned with taxonomy patterns).
  - Map to exit codes in recovery policy.
  **Must NOT do**:
  - Do not remap existing codes silently.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: contract + release gate impact.
  - Skills: `[]`
  - Omitted: `writing`.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 9,11 | Blocked By: none

  **References**:
  - API/Type: `src/errors/taxonomy.zig`
  - API/Type: `src/recovery/policy.zig`
  - Pattern: `src/capture/command_json.zig` error envelope fields

  **Acceptance Criteria**:
  - [ ] New overlay codes listed in taxonomy and mapped in policy.
  - [ ] Unit tests validate exit-code mapping.

  **QA Scenarios**:
  ```
  Scenario: Overlay timeout
    Tool: Bash
    Steps: Force helper to exceed timeout and run capture area.
    Expected: JSON error code matches ERR_OVERLAY_TIMEOUT and exit code matches policy.
    Evidence: .sisyphus/evidence/task-4-overlay-timeout.json

  Scenario: Overlay protocol invalid
    Tool: Bash
    Steps: Force helper malformed payload mode.
    Expected: JSON error code ERR_OVERLAY_PROTOCOL_INVALID.
    Evidence: .sisyphus/evidence/task-4-overlay-protocol-error.json
  ```

  **Commit**: YES | Message: `feat(errors): add deterministic overlay failure taxonomy` | Files: [errors/taxonomy, recovery/policy, tests]

- [x] 5. Implement Raylib overlay bootstrap and lifecycle

  **What to do**:
  - Create overlay runtime module with deterministic init/shutdown.
  - Implement timeout-aware startup and graceful cancellation handling.
  **Must NOT do**:
  - Do not couple to capture backend writing logic.

  **Recommended Agent Profile**:
  - Category: `visual-engineering` - Reason: rendering/lifecycle heavy.
  - Skills: `[]`
  - Omitted: `writing`.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 6,7 | Blocked By: 1,3

  **References**:
  - Pattern: `src/overlay/overlay.zig` current lifecycle semantics.
  - External: raylib Wayland issue and mitigations from librarian findings.

  **Acceptance Criteria**:
  - [ ] Helper process starts and exits deterministically under Niri.
  - [ ] Timeout path returns taxonomy-aligned failure.

  **QA Scenarios**:
  ```
  Scenario: Startup happy path
    Tool: Bash
    Steps: Launch helper in smoke mode on Niri env.
    Expected: Exit 0 and emits `status=ready` marker.
    Evidence: .sisyphus/evidence/task-5-overlay-bootstrap.txt

  Scenario: Startup timeout
    Tool: Bash
    Steps: Inject startup delay > timeout.
    Expected: ERR_OVERLAY_TIMEOUT deterministic output.
    Evidence: .sisyphus/evidence/task-5-overlay-bootstrap-timeout.txt
  ```

  **Commit**: YES | Message: `feat(overlay): add raylib helper bootstrap lifecycle` | Files: [new overlay runtime modules]

- [x] 6. Implement Clay HUD layout (MVP controls)

  **What to do**:
  - Render dim layer, selection frame, live size badge, tool strip, aspect dropdown UI.
  - Include disabled placeholders for non-MVP actions (OCR/Record) to match visual target without behavior scope creep.
  **Must NOT do**:
  - Do not implement full annotation editor.

  **Recommended Agent Profile**:
  - Category: `visual-engineering` - Reason: UI layout/rendering.
  - Skills: `[]`
  - Omitted: `deep`.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 7 | Blocked By: 3,5

  **References**:
  - External: Clay raylib renderer reference.
  - Pattern: existing selection result contract in `src/selection/selection.zig`.

  **Acceptance Criteria**:
  - [ ] HUD draws consistently at 60fps target in MVP idle interactions.
  - [ ] Aspect dropdown selection updates internal selection constraint state.

  **QA Scenarios**:
  ```
  Scenario: HUD visible with controls
    Tool: Bash
    Steps: Run helper with screenshot dump frame command.
    Expected: Frame evidence includes tool strip + aspect dropdown region.
    Evidence: .sisyphus/evidence/task-6-hud-frame.png

  Scenario: Disabled placeholder controls
    Tool: Bash
    Steps: Trigger OCR/Record placeholders.
    Expected: Non-fatal warning token and no state corruption.
    Evidence: .sisyphus/evidence/task-6-hud-placeholders.json
  ```

  **Commit**: YES | Message: `feat(overlay): implement clay hud mvp controls` | Files: [overlay UI files/assets]

- [x] 7. Implement selection interaction engine for Raylib helper

  **What to do**:
  - Implement drag-to-select, resize handles, confirm/cancel keyboard/mouse pathways.
  - Emit final geometry respecting aspect constraints.
  **Must NOT do**:
  - Do not bypass contract parser boundary.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: interaction/state correctness.
  - Skills: `[]`
  - Omitted: `writing`.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 8,9 | Blocked By: 1,5,6

  **References**:
  - Pattern: selection structs in `src/selection/selection.zig`.
  - Test: `scripts/qa/assert-overlay-base-selection.sh` expected geometry semantics.

  **Acceptance Criteria**:
  - [ ] Drag/select returns non-zero geometry.
  - [ ] Esc path returns cancel deterministically.

  **QA Scenarios**:
  ```
  Scenario: Drag selection
    Tool: Bash
    Steps: Run helper in simulated pointer script mode from (100,100) to (500,400).
    Expected: Output geometry width=400 height=300.
    Evidence: .sisyphus/evidence/task-7-selection-drag.json

  Scenario: Escape cancel
    Tool: Bash
    Steps: Inject Esc event in helper test mode.
    Expected: status=cancel and parent maps to ERR_SELECTION_CANCELLED.
    Evidence: .sisyphus/evidence/task-7-selection-cancel.json
  ```

  **Commit**: YES | Message: `feat(overlay): add selection interaction engine` | Files: [overlay interaction modules/tests]

- [x] 8. Integrate helper runner into `src/overlay/overlay.zig`

  **What to do**:
  - Replace direct `slurp` shell path with helper-first strategy:
    1) helper attempt
    2) on configured failure classes fallback to `slurp`
    3) preserve dry-run/simulate-cancel deterministic behavior
  **Must NOT do**:
  - Do not remove slurp fallback in MVP.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: critical boundary integration.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: NO | Wave 3 | Blocks: 9,11,12 | Blocked By: 1,2,7

  **References**:
  - Pattern: `src/overlay/overlay.zig` existing return semantics.
  - API/Type: `src/selection/selection.zig`.

  **Acceptance Criteria**:
  - [ ] Helper success path returns correct `SelectionResult`.
  - [ ] Fallback path engages when helper unavailable.

  **QA Scenarios**:
  ```
  Scenario: Helper success preferred over slurp
    Tool: Bash
    Steps: Enable helper and run area capture selection.
    Expected: Evidence marker indicates helper path used.
    Evidence: .sisyphus/evidence/task-8-overlay-helper-path.json

  Scenario: Helper unavailable fallback
    Tool: Bash
    Steps: Force helper binary missing and run selection.
    Expected: slurp fallback used or deterministic overlay-unavailable error per policy.
    Evidence: .sisyphus/evidence/task-8-overlay-fallback-path.json
  ```

  **Commit**: YES | Message: `feat(overlay): integrate helper-first runner with fallback` | Files: [overlay.zig + related integration tests]

- [x] 9. Preserve capture command contracts with new overlay integration

  **What to do**:
  - Validate `capture area` output envelope unchanged despite helper integration.
  - Keep guard ordering and warning behavior deterministic.
  **Must NOT do**:
  - Do not alter key names/structure in `writeSuccessJson` and `writeErrorJson` outputs.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: broad regression prevention.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: NO | Wave 3 | Blocks: 11,12,15 | Blocked By: 2,4,7,8

  **References**:
  - Pattern: `src/capture/command.zig`
  - Pattern: `src/capture/command_json.zig`
  - Test: `scripts/qa/assert-capture-result-schema.sh`

  **Acceptance Criteria**:
  - [ ] Existing schema assertions pass without modifications to expected structure.
  - [ ] Cancel/timeout/error mappings remain deterministic.

  **QA Scenarios**:
  ```
  Scenario: Contract preservation
    Tool: Bash
    Steps: Run area/fullscreen/window contract assertion scripts.
    Expected: All pass without schema baseline changes.
    Evidence: .sisyphus/evidence/task-9-contract-preservation.json

  Scenario: Cancel deterministic exit
    Tool: Bash
    Steps: Simulate cancel in helper path.
    Expected: exit code maps to ERR_SELECTION_CANCELLED policy.
    Evidence: .sisyphus/evidence/task-9-cancel-determinism.txt
  ```

  **Commit**: YES | Message: `fix(capture): preserve json contracts across overlay integration` | Files: [capture command/tests/qa scripts]

- [x] 10. Add Noctalia action hooks for open folder and clipboard image

  **What to do**:
  - Add MVP actions exposed in helper UI:
    - open output folder (`xdg-open` with compositor-safe fallback)
    - open image currently in clipboard from pipeline output temp file
  - Keep actions non-blocking for core capture completion.
  **Must NOT do**:
  - Do not make Noctalia plugin mandatory.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: integration glue and failure handling.
  - Skills: `[]`
  - Omitted: `deep`.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 15 | Blocked By: none

  **References**:
  - Pattern: `integrations/noctalia/noctalia-action-adapter.sh`
  - Pattern: `scripts/qa/assert-noctalia-actions.sh`

  **Acceptance Criteria**:
  - [ ] Actions execute when available and return deterministic non-fatal warnings when unavailable.
  - [ ] Existing Noctalia optionality tests continue passing.

  **QA Scenarios**:
  ```
  Scenario: Open folder happy path
    Tool: Bash
    Steps: Trigger open-folder action with xdg-open available.
    Expected: action result ok=true and capture result remains ok=true.
    Evidence: .sisyphus/evidence/task-10-open-folder.json

  Scenario: Open clipboard image failure path
    Tool: Bash
    Steps: Disable opener utility and trigger action.
    Expected: non-fatal action error token; capture remains successful.
    Evidence: .sisyphus/evidence/task-10-open-clipboard-error.json
  ```

  **Commit**: YES | Message: `feat(noctalia): add open folder and clipboard image mvp actions` | Files: [noctalia integration + qa]

- [x] 11. Add interactive overlay QA suite and deterministic fixtures

  **What to do**:
  - Add/extend scripts to validate helper interactive path explicitly (not dry-run).
  - Capture deterministic evidence for helper-path success/cancel/error/fallback.
  **Must NOT do**:
  - Do not rely solely on existing dry-run overlay checks.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: QA framework evolution.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: NO | Wave 4 | Blocks: 16 | Blocked By: 2,4,8,9

  **References**:
  - Pattern: `scripts/qa/assert-overlay-base-selection.sh`
  - Pattern: `scripts/qa/run-integration-tests.sh`
  - Pattern: `scripts/qa/run-e2e-niri.sh`

  **Acceptance Criteria**:
  - [ ] Integration/e2e suites include helper interactive subchecks.
  - [ ] Failure summary points to failing subcheck IDs/commands.

  **QA Scenarios**:
  ```
  Scenario: Interactive helper lane pass
    Tool: Bash
    Steps: Run integration and e2e with full profile.
    Expected: helper interactive subchecks pass.
    Evidence: .sisyphus/evidence/task-11-interactive-overlay-report.json

  Scenario: Forced helper protocol error
    Tool: Bash
    Steps: Enable helper malformed payload mode.
    Expected: deterministic overlay protocol error surfaced.
    Evidence: .sisyphus/evidence/task-11-interactive-overlay-error.txt
  ```

  **Commit**: YES | Message: `test(overlay): add interactive helper qa lane` | Files: [qa scripts + evidence schema updates]

- [x] 12. Add real interactive first-paint performance gates

  **What to do**:
  - Add benchmark script for real helper startup/first-paint path.
  - Keep thresholds explicit and enforced in performance gate script.
  **Must NOT do**:
  - Do not use `--dry-run` as proxy metric for interactive overlay first-paint.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: perf contract design.
  - Skills: `[]`
  - Omitted: `writing`.

  **Parallelization**: Can Parallel: YES | Wave 4 | Blocks: 16 | Blocked By: 8,9

  **References**:
  - Pattern: `scripts/qa/benchmark-overlay-first-paint.sh`
  - Evidence: `.sisyphus/evidence/task-8-overlay-latency.json`

  **Acceptance Criteria**:
  - [ ] New interactive benchmark outputs JSON with p95/p99.
  - [ ] Performance gate fails with deterministic token when budgets exceeded.

  **QA Scenarios**:
  ```
  Scenario: Budget pass
    Tool: Bash
    Steps: Run interactive overlay benchmark with production thresholds.
    Expected: pass=true and thresholds respected.
    Evidence: .sisyphus/evidence/task-12-overlay-interactive-latency.json

  Scenario: Budget fail
    Tool: Bash
    Steps: Run benchmark with intentionally strict thresholds.
    Expected: deterministic perf failure token.
    Evidence: .sisyphus/evidence/task-12-overlay-interactive-latency-error.txt
  ```

  **Commit**: YES | Message: `perf(overlay): enforce interactive first-paint gates` | Files: [perf scripts + gate wiring]

- [ ] 13. Add multi-output and fractional-scaling geometry fixtures

  **What to do**:
  - Add fixtures and assertions for coordinate normalization under multi-output setups.
  - Ensure geometry round-trips correctly to backend capture request.
  **Must NOT do**:
  - Do not leave conversion logic duplicated across modules.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: correctness edge cases.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: YES | Wave 4 | Blocks: 16 | Blocked By: 7

  **References**:
  - API/Type: `src/selection/selection.zig`
  - API/Type: `src/capture/types.zig:AreaGeometry`
  - Pattern: backend geometry formatter in `src/backends/capture_backend.zig`

  **Acceptance Criteria**:
  - [ ] Fixture-driven tests validate expected geometry in multi-output scenarios.
  - [ ] No regressions in area capture path dimensions.

  **QA Scenarios**:
  ```
  Scenario: Multi-output geometry normalization
    Tool: Bash
    Steps: Run fixture suite with synthetic monitor layout definitions.
    Expected: geometry normalization outputs expected coordinates.
    Evidence: .sisyphus/evidence/task-13-multioutput-geometry.json

  Scenario: Fractional scaling edge
    Tool: Bash
    Steps: Run fixture with 1.25/1.5 scaling definitions.
    Expected: deterministic rounded geometry and no negative dimensions.
    Evidence: .sisyphus/evidence/task-13-fractional-scaling.json
  ```

  **Commit**: YES | Message: `test(overlay): add multi-output scaling geometry fixtures` | Files: [fixtures/tests]

- [ ] 14. Update documentation for overlay architecture and QA operations

  **What to do**:
  - Update `DEV.md` and relevant spec docs with new overlay flow, profiles, and debug knobs.
  - Document helper contract and fallback/error behavior.
  **Must NOT do**:
  - Do not leave undocumented env flags that alter behavior.

  **Recommended Agent Profile**:
  - Category: `writing` - Reason: doc-heavy task.
  - Skills: `[]`
  - Omitted: `deep`.

  **Parallelization**: Can Parallel: YES | Wave 5 | Blocks: 15 | Blocked By: 2,8,11,12

  **References**:
  - Pattern: `DEV.md` existing QA section.
  - Specs: `spec/architecture.md`, `spec/testing.md`, `spec/wayland-niri-integration.md`.

  **Acceptance Criteria**:
  - [ ] Docs include contract, env flags, and troubleshooting for helper path.
  - [ ] Examples for fast/full/debug QA profiles and artifacts policy are current.

  **QA Scenarios**:
  ```
  Scenario: Doc command examples executable
    Tool: Bash
    Steps: Execute all new documented command snippets.
    Expected: commands run successfully or produce documented deterministic failures.
    Evidence: .sisyphus/evidence/task-14-doc-snippets.txt

  Scenario: Missing-doc regression check
    Tool: Bash
    Steps: Run grep checks for required flags/tokens in docs.
    Expected: all required entries present.
    Evidence: .sisyphus/evidence/task-14-doc-coverage.txt
  ```

  **Commit**: YES | Message: `docs(overlay): document raylib clay flow and qa controls` | Files: [DEV.md, spec/*]

- [ ] 15. Update release readiness checklist for overlay helper path

  **What to do**:
  - Extend release-readiness script to include helper-path checks and action hooks.
  - Ensure blocked-path behavior remains deterministic with `ERR_RELEASE_BLOCKED`.
  **Must NOT do**:
  - Do not weaken existing readiness gates.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: release gate integrity.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: NO | Wave 5 | Blocks: 16 | Blocked By: 9,10,14

  **References**:
  - Pattern: `scripts/qa/release-readiness-capture-fix.sh`
  - Evidence pattern: `.sisyphus/evidence/task-12-release-readiness.json`

  **Acceptance Criteria**:
  - [ ] Readiness report includes helper-path status checks.
  - [ ] Forced block path remains validated.

  **QA Scenarios**:
  ```
  Scenario: Release readiness pass
    Tool: Bash
    Steps: Run readiness script in normal mode.
    Expected: ready=true and blocking_issues=0.
    Evidence: .sisyphus/evidence/task-15-release-readiness.json

  Scenario: Forced block
    Tool: Bash
    Steps: Run with force block env.
    Expected: deterministic ERR_RELEASE_BLOCKED and evidence error log.
    Evidence: .sisyphus/evidence/task-15-release-readiness-error.txt
  ```

  **Commit**: YES | Message: `chore(release): extend readiness checks for overlay helper` | Files: [release readiness script + evidence paths]

- [ ] 16. Final regression sweep across command families

  **What to do**:
  - Run full matrix (unit/integration/e2e/perf/release-readiness) with profiles and artifact policies.
  - Confirm no regressions in history/daemon/capabilities outputs.
  **Must NOT do**:
  - Do not skip failing subchecks.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: broad system validation.
  - Skills: `[]`
  - Omitted: `visual-engineering`.

  **Parallelization**: Can Parallel: NO | Wave 5 | Blocks: Final Verification | Blocked By: 11,12,13,15

  **References**:
  - Pattern: `scripts/qa/run-all-tests.sh`
  - Pattern: `scripts/qa/run-performance-gates.sh`
  - Pattern: `scripts/qa/release-readiness-capture-fix.sh`

  **Acceptance Criteria**:
  - [ ] All matrix layers pass in full profile.
  - [ ] Fast/debug profile behavior matches policy.

  **QA Scenarios**:
  ```
  Scenario: Full profile pass
    Tool: Bash
    Steps: SHAULA_QA_PROFILE=full bash scripts/qa/run-all-tests.sh
    Expected: matrix pass=true with no failed subchecks.
    Evidence: .sisyphus/evidence/task-16-full-regression.json

  Scenario: Debug profile keep artifacts
    Tool: Bash
    Steps: SHAULA_QA_PROFILE=debug QA_KEEP_ARTIFACTS=1 bash scripts/qa/run-all-tests.sh
    Expected: pass=true and /tmp/shaula/runs/latest exists.
    Evidence: .sisyphus/evidence/task-16-debug-regression.txt
  ```

  **Commit**: YES | Message: `test(regression): validate full matrix after raylib clay integration` | Files: [evidence + any minimal fixes]

## Final Verification Wave (MANDATORY — after ALL implementation tasks)
> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.
> **Do NOT auto-proceed after verification. Wait for user's explicit approval before marking work complete.**
> **Never mark F1-F4 as checked before getting user's okay.**
- [ ] F1. Plan Compliance Audit — oracle
- [ ] F2. Code Quality Review — unspecified-high
- [ ] F3. Real Manual QA — unspecified-high (+ playwright if UI)
- [ ] F4. Scope Fidelity Check — deep

## Commit Strategy
- Small, task-scoped commits per TODO with deterministic test evidence.
- Keep contract/taxonomy updates isolated from rendering/UI commits.
- Merge order follows dependency matrix; no squash of unrelated tasks.

## Success Criteria
- Raylib+Clay overlay helper is active for `capture area` interactive path on Niri.
- Existing public CLI JSON schemas and exit-code semantics remain stable.
- Interactive overlay perf gates pass at defined thresholds.
- QA matrix full/fast/debug profiles behave deterministically with clear failure summaries.
- Noctalia remains optional; new actions are non-blocking to capture success.
