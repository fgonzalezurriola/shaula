## 2026-04-19T17:43:18Z Task 1 - runtime capture backend
- Decision: Keep capture success/failure payload contract untouched and enforce backend availability through existing failure envelope (`ERR_CAPTURE_BACKEND_UNAVAILABLE`, retryable=true).
- Decision: Add explicit backend kind `stub` with canonical `backend_used="__stub__"` so forced stub mode is observable and deterministic in JSON errors.
- Decision: Add QA gates (`assert-no-runtime-stub.sh`, `assert_png_not_stub_signature.py`) that fail if runtime returns old stub signature or forced stub produces output file.
- Decision: Export root module aliases in `src/main.zig` to support file-targeted backend tests under Zig 0.16 without changing runtime command behavior.
## 2026-04-19T19:14:46Z Task 1 correction - runtime boundary
- Decision: Production capture path must execute an external runtime backend command source (`SHAULA_RUNTIME_CAPTURE_HELPER`) and must not embed/write synthetic PNG payload bytes in Zig backend code.
- Decision: Any runtime helper failure (missing helper, spawn failure, non-zero exit, abnormal termination) maps to `ERR_CAPTURE_BACKEND_UNAVAILABLE` with `retryable=true` and output cleanup.
- Decision: Keep forced stub policy unchanged: `SHAULA_CAPTURE_BACKEND=__stub__` always returns deterministic unavailable error and produces no file.
- Decision: Preserve existing capture JSON field schema exactly to avoid regressions in `capture command` and `post_capture` pipelines.

## 2026-04-19T20:07:07Z Task 2 - capabilities/execution strict contract
- Decision: `src/capabilities/runtime.zig` is the single source of truth for runtime capability flags (`area/fullscreen/window`), backend resolution, and canonical backend label emission.
- Decision: Preserve JSON schema keys while enforcing strict runtime behavior; unsupported modes now fail with deterministic `ERR_CAPTURE_MODE_UNSUPPORTED` instead of backend-specific fallthrough errors.
- Decision: Canonical backend identifier is standardized as `niri-wayland-direct` for both `capabilities` and capture success/failure payloads to eliminate contract drift.
- Decision: Keep scope bounded to Task 2 by updating only capability/capture parity paths and QA assertions related to that contract.

## 2026-04-19T20:29:52Z Task 3 - capture content integrity QA
- Decision: Implement PNG decode validation directly in `assert-capture-content-validity.sh` (embedded Python) to keep checks deterministic and self-contained without adding extra top-level tooling beyond Task 3 scope.
- Decision: Keep `assert_png_not_stub_signature.py` as the canonical stub detector and reuse its exported `STUB_SIGNATURE` constant when building negative-path fixture bytes to guarantee deterministic machine-verifiable rejection.
- Decision: Restrict luminance/non-black assertions to controlled fixture mode (`colorful-grid`) and record whether the check was applied in evidence JSON, explicitly avoiding a universal real-scene brightness rule.
- Decision: Add Task 3 validation to integration orchestration so `run-all-tests.sh` includes this gate transitively while preserving existing capture JSON schema keys unchanged.

## 2026-04-19T20:52:57Z Task 4 - daemon status IPC truth
- Decision: Treat `daemon.status` IPC as the source of truth for CLI `daemon status`; socket existence remains a fast precheck only and is no longer used as final state inference.
- Decision: Preserve daemon status output contract shape while replacing only value source: top-level `state` and `result.state` mirror daemon IPC state, and `result.ipc_version` is taken from IPC response with protocol fallback.
- Decision: Reuse existing stop-flow transport pattern (Unix stream + single-line JSON request/response) for consistency and reduced lifecycle risk.
- Decision: For orphan/non-responsive socket paths, enforce deterministic CLI failures using existing taxonomy (`ERR_DAEMON_NOT_RUNNING` on connect/write failures, `ERR_IPC_TIMEOUT` on timeout/invalid IPC response).

## 2026-04-19T21:00:12Z Task 5 - default output path Pictures/Shaula
- Decision: Default capture path (only when `--output` is omitted) is now strictly `HOME/Pictures/Shaula/capture-{mode}-{timestamp}.png` to align product default persistence with plan requirements.
- Decision: Remove any implicit `/tmp` fallback for default captures; missing/invalid/non-writable HOME-derived defaults are surfaced as deterministic `ERR_OUTPUT_PATH_INVALID`.
- Decision: Keep explicit `--output` contract untouched to avoid regressions for callers that already provide custom absolute paths.

## 2026-04-19T21:16:50Z Task 6 - history Top-N=20
- Decision: Keep storage file path unchanged (`/tmp/shaula/history/latest.v1`) and evolve content semantics to newline-delimited Top-N entries to minimize blast radius while satisfying retention requirements.
- Decision: Enforce deterministic retention at write time (`storeLatest`) by prepending the new entry and trimming to `default_top_n=20`, so readers and CLI outputs remain stable without additional sorting heuristics.
- Decision: Keep `history show --id latest` as an explicit alias to first list entry instead of introducing new id semantics, preserving compatibility and acceptance criteria.
## 2026-04-19T21:30:00Z Task 7 - overlay base selection
- Decision: Use `slurp` binary natively in `std.process.run` for actual area selection and dimming overlay. This natively supports Wayland UI selection bounds without bloating the project or expanding the "v1 base scope" to full GUI rendering toolkits.
- Decision: Add standard `.geometry: { x, y, width, height }` format to the dry-run output so downstream tools can consume the bounding box properly.
- Decision: Ensure the `runSelection` signature receives `allocator`, `io`, and `environ` correctly, even when mocking/simulating returns, to maintain standard architectural pattern without leaking global states.

## 2026-04-19T22:16:43Z Task 8 - shell artifact guard pre-capture
- Decision: Place shell artifact guard in capture command pre-execution flow (`src/capture/command.zig`) rather than overlay internals to guarantee protection applies uniformly to area/fullscreen/window captures and any future Noctalia-triggered capture path.
- Decision: Keep handshake optional by default to preserve capture continuity when shell callback is unavailable, but expose strict mode (`SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE=1`) for deterministic timeout-path validation and hard precondition enforcement contexts.
- Decision: Preserve capture JSON contract shape; represent guard path selection via warnings only and map unmet precondition to deterministic taxonomy error `ERR_CAPTURE_PRECONDITION_TIMEOUT` (retryable, bounded timeout class behavior).
- Decision: Use deterministic artifact detection in QA through synthetic magenta panel marker injection in runtime helper rather than non-deterministic visual assumptions from real shell animation frames.

## 2026-04-19T22:46:38Z Task 9 - Noctalia MVP plugin actions
- Decision: Make Noctalia integration contract-driven via a standalone adapter script that returns deterministic JSON for both `--menu` and `--action` paths, with explicit `shaula_argv` arrays as source of truth.
- Decision: Keep plugin optional by ensuring all execute paths call the existing Shaula binary (`zig-out/bin/shaula`) and by validating `--without-plugin` capture flows independently in QA.
- Decision: Preserve minimal control-center surface by limiting plugin menu payload to exactly five actions and a `minimal=true` marker, avoiding any extra presentation behaviors.
- Decision: Treat `capture-window` as an expected unsupported execution in this runtime capability context and validate deterministic failure taxonomy (`ERR_CAPTURE_MODE_UNSUPPORTED`) instead of masking it at plugin level.

## 2026-04-19T23:04:29Z Task 10 - QA matrix consolidation
- Decision: Introduce per-layer JSON reports (`task-10-layer-integration-report.json`, `task-10-layer-e2e-niri-report.json`) and make `run-all-tests.sh` consume them, so consolidated matrix generation is deterministic and layer failures are machine-verifiable.
- Decision: Keep Task 10 scope strictly in QA orchestration and gate wiring; no runtime product code or docs/spec changes were introduced.
- Decision: Preserve strict capability semantics while maintaining capture-integrity coverage by adapting `assert-no-runtime-stub.sh` to validate deterministic forced-stub failure without assuming a single error code path.

## 2026-04-19T23:16:15Z Task 11 - spec contract alignment
- Decision: Treat `spec/architecture.md` as the canonical place for final runtime contract locks and mirror those locks in `requirements/testing/wayland` docs without introducing alternative wording that could imply different behavior.
- Decision: Keep Noctalia documented as optional MVP adapter integration with a fixed five-action mapping (`capture-area`, `capture-fullscreen`, `capture-window`, `open-last`, `history`) and explicit ownership boundary where capture logic remains in Shaula core.
- Decision: Keep Task 11 limited to documentation consistency, no runtime or behavior changes, and validate via cross-link script plus contractual grep assertions.

## 2026-04-19T23:35:26Z Task 12 - release readiness closure
- Decision: Add a dedicated deterministic release checklist script (`scripts/qa/release-readiness-capture-fix.sh`) instead of embedding this logic into `run-all-tests.sh`, so release gating remains explicit, auditable, and scoped to remediation closure.
- Decision: Represent blockers as structured check IDs plus textual details and compute final readiness strictly from blocker count (`ready = blocking_issues == 0`), ensuring reproducible CI behavior.
- Decision: Include an explicit forced-block path (`SHAULA_RELEASE_READINESS_FORCE_BLOCK=1`) to validate deterministic error taxonomy (`ERR_RELEASE_BLOCKED`) without mutating prior evidence artifacts.

## 2026-04-19T23:59:00Z Task F4 - scope fidelity verdict
- Finding: Scope guardrails held end-to-end (overlay stayed v1 base, Noctalia remained optional, and no silent stub/runtime fallback or non-Niri expansion was introduced), so F4 verdict is APPROVE.
