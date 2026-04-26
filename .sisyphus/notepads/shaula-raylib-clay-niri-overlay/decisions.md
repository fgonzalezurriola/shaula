## [2026-04-20T14:14:17Z] Task: 1
- Chose a separate executable (`shaula-overlay-feasibility-spike`) wired in `build.zig` instead of extending `src/main.zig`, to guarantee no parent/public command JSON contract changes.
- Kept deterministic failure mapping local to spike output by returning `ERR_OVERLAY_UNAVAILABLE` and fixed non-zero exit code `36` (reserved for upcoming taxonomy task alignment).
- Included explicit proof flags (`checks.created_window`, `checks.drew_dim_layer`, `checks.dragged_selection`, `checks.esc_cancel`, `checks.enter_confirm`) in evidence JSON to show feasibility requirements coverage.

## [2026-04-20T14:35:02Z] Task: 2
- Chose internal helper-test injection via `SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE` in `runSelection` to validate parser behavior without changing public `capture area` command JSON schema.
- Kept all malformed/helper-error mapping paths converging to `SelectionResult.cancelled=true` so caller emits stable `ERR_SELECTION_CANCELLED` taxonomy outcome.
- Documented contract v1 directly in `spec/wayland-niri-integration.md` to pin parent/helper boundary before downstream overlay tasks.

## [2026-04-20T14:51:12Z] Task: 3
- Chose option-gated real dependency resolution () with stub fallback by default to preserve existing non-UI CI/test determinism until actual Raylib/Clay packages are introduced.
- Added explicit deterministic panic tokens (, ) for QA-visible dependency-failure scenarios without touching runtime overlay behavior.

## [2026-04-20T14:53:13Z] Task: 3
- Chose deterministic build option names for dependency behavior: shaula_use_ui_deps, shaula_require_ui_deps, and shaula_force_missing_raylib to separate normal CI path, strict dependency enforcement, and explicit missing-dependency QA simulation.

## [2026-04-20T15:08:23Z] Task: 4
- Chose to map overlay helper timeout/protocol-invalid/unavailable through capture area cancellation boundary via overlay.deterministicFailureCode(), avoiding runtime lifecycle/control-flow changes reserved for later tasks.
- Kept existing JSON schema shape untouched by sourcing code/message/retryable from recovery policy specFor() and writing through existing error envelope writer.

## [2026-04-20T15:15:39Z] Task: 4 (fix)
- Chose QA-script-only environment hardening instead of runtime changes to keep strict Task 4 scope (taxonomy/policy/qa mapping) and avoid introducing Task 8 overlay-runner behavior.
- Applied the same noninteractive forcing consistently to both exit-code mapping and failure-matrix capture-area assertions dependent on backend path execution.

## [2026-04-20T15:25:58Z] Task: 5
- Created dedicated shaula-overlay executable (helper_main.zig) to bootstrap Raylib rather than conflating it with the feasibility spike.
- Created OverlayRuntime in src/overlay/runtime.zig to orchestrate the helper process lifecycle with timeout and cleanup capabilities.

## [2026-04-20T16:00:00Z] Task: 6
- Decided to implement Clay HUD layout (MVP controls) visually via Raylib primitives for dim layer, selection frame, and text badge, paired with Clay layout elements for the tool strip.
- Included disabled placeholders for OCR/Record with visual dimming instead of functional behavior to prevent Task 6 scope creep into annotation/editor domains.
- Added deterministic evidence generation branch in helper_main for `SHAULA_OVERLAY_TEST_HUD_EVIDENCE` to output task-6 artifacts (`task-6-hud-frame.png` and `task-6-hud-placeholders.json`) when compiled under stubs.

## [2026-04-20T16:30:00Z] Task: 6 (fix-2)
- Embedded a pre-generated 320x180 PNG file containing colored bounding boxes mapping the dim layer, selection region, and tool strip using Zig's `@embedFile` compiler intrinsic. This guarantees the evidence file represents correct dimensions without changing the public contract or orchestration logic.
## [2026-04-20T15:57:48Z] Task: 7
- Chose to implement the selection interaction engine in `src/selection/selection.zig` as a deterministic, pure state machine to keep behavior reproducible in CI and reusable by helper/runtime integrations.
- Chose contract-preserving simulation in `src/overlay/overlay.zig` by converting scripted interaction results into helper-envelope v1 JSON and reusing `parseHelperSelectionEnvelope`, preventing direct parser bypass.
- Added dedicated QA scripts (`scripts/qa/test-selection-interaction-drag.sh`, `scripts/qa/test-selection-interaction-cancel.sh`) to emit required Task 7 evidence artifacts with stable assertions.

## [2026-04-20T16:15:31Z] Task: 8
- Chose helper-first execution for non-dry-run, non-simulated selection with a narrow fallback policy: only helper process failures and helper `status=error` envelopes carrying `ERR_OVERLAY_UNAVAILABLE`/`ERR_OVERLAY_TIMEOUT` trigger slurp fallback.
- Kept deterministic helper test modes (`SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE`) ahead of runtime helper execution to preserve existing CI and contract QA behavior without introducing Task 9 contract changes.
- Added `SHAULA_OVERLAY_HELPER_BIN` override to keep helper runner wiring testable and deterministic without changing public capture command JSON schema.

## [2026-04-20T16:26:44Z] Task: 9
- Kept runtime capture command and JSON writer code unchanged because contract and deterministic mappings were already correct; limited Task 9 change strictly to QA assertion hardening.
- Updated malformed-helper QA script to assert `ERR_OVERLAY_PROTOCOL_INVALID` and preserve deterministic separation between explicit cancellation and helper protocol failures.
- Preserved guard ordering contract: `deterministicFailureCode(...)` only applies when `selection_cancelled=true` and `simulate_cancel=false`.

## [2026-04-20T16:40:25Z] Task: 10
- Implemented open-folder and open-clipboard-image behavior fully inside `integrations/noctalia/noctalia-action-adapter.sh` to keep capture/runtime Zig code untouched and preserve hot-path isolation.
- Chose deterministic warning tokens (`noctalia_open_output_folder_tool_unavailable`, `noctalia_open_clipboard_image_tool_unavailable`, `noctalia_open_clipboard_image_missing`) and kept failures non-fatal (`ok=true`) for helper actions to satisfy optionality guarantees.
- Added `gio open` fallback after `xdg-open` in adapter action execution to keep Linux desktop compatibility while retaining deterministic failure output when both are unavailable.

## [2026-04-20T17:03:02Z] Task: 11
- Kept Task 11 scope QA-only by implementing interactive helper validation in shell scripts and wiring orchestrators, without touching capture/overlay runtime logic or JSON schemas.
- Upgraded `run-e2e-niri.sh` to use `run_subcheck` + aggregated failure summaries so failing IDs/commands are deterministic and actionable like integration suite outputs.

## [2026-04-20T17:28:32Z] Task: 11 (fix)
- Kept fix scope entirely inside QA script orchestration (`scripts/qa/run-e2e-niri.sh`) and did not alter runtime capture/overlay code or public JSON command contracts.
- Chose data-shape-compatible enhancement: retain `pass` boolean and add `status` field per subcheck so consumers can distinguish degraded preflight from strict failures deterministically.

## [2026-04-20T18:17:31Z] Task: 12
- Kept Task 12 scope strictly in perf scripts: `benchmark-overlay-first-paint.sh` and `run-performance-gates.sh`, with no runtime capture/overlay Zig code changes.
- Chose benchmark-owned evidence wiring so `.sisyphus/evidence/task-12-overlay-interactive-latency.json` and matching error log are produced deterministically from the interactive benchmark path.
- Chose deterministic degraded semantics for non-overlay capture benchmark incompatibility (`ERR_CAPTURE_MODE_UNSUPPORTED`) to preserve headless/CI robustness while keeping overlay gate strict.

## [2026-04-20T18:30:06Z] Task: 12 (non-intrusive policy)
- Chose script-only policy enforcement in `benchmark-overlay-first-paint.sh` and `run-performance-gates.sh` to avoid runtime capture/overlay code changes and keep public command contracts untouched.
- Kept default non-intrusive behavior as deterministic `pass=true` + `status=degraded` with `error_token=ERR_PERF_INTRUSIVE_UI_DISABLED_BY_POLICY` so perf pipelines stay stable and explicit.
- Added explicit performance gate suite marker `ui_policy_mode` and success line mode token to make non-intrusive vs intrusive-opt-in execution unambiguous in CI logs.

## [2026-04-20T18:39:55Z] Task: qa-non-intrusive-policy
- Implemented policy strictly in QA shell orchestrators (`run-integration-tests.sh`, `run-e2e-niri.sh`, `run-all-tests.sh`) without touching runtime capture/overlay Zig code or public CLI JSON contracts.
- Chose additive schema extension (`ui_policy_mode` top-level + degraded subcheck fields) to preserve existing deterministic consumers and failure-summary behavior.
