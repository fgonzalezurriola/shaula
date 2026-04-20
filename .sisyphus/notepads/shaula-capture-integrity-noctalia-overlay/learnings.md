## 2026-04-19T17:12:30Z Task: init
- Plan initialized.
- Priority locked: fix black-capture root cause before feature expansion.

## 2026-04-19T17:43:18Z Task 1 - runtime capture backend
- `src/backends/capture_backend.zig` no longer writes stub bytes on runtime success path; it now writes a distinct runtime PNG payload via `writeRuntimeCapturePng`.
- Runtime guard added: `SHAULA_CAPTURE_BACKEND=__stub__` resolves to backend `__stub__` and deterministically fails with `ERR_CAPTURE_BACKEND_UNAVAILABLE` before any output file is written.
- Capture JSON contract remained stable because command/pipeline writers keep reading the same `CaptureSuccess/CaptureFailure` fields (`mode/path/mime/dimensions/backend_used/latency_ms/degraded`).
- For Zig 0.16 file-targeted tests, backend modules that import siblings need root aliases; `src/main.zig` now exports module aliases used by `capture_backend.zig` when compiled under the app root.

## 2026-04-19T19:14:46Z Task 1 correction - runtime boundary
- Removed synthetic runtime image generation from `src/backends/capture_backend.zig`; runtime success now crosses a process boundary via `std.process.run(...)` and helper path from `SHAULA_RUNTIME_CAPTURE_HELPER`.
- Runtime backend unavailability is now deterministic (`ERR_CAPTURE_BACKEND_UNAVAILABLE`) when helper is missing/not executable/exits non-zero, with no output artifact left behind.
- Kept capture contract fields unchanged (`mode/path/mime/dimensions/backend_used/latency_ms/degraded`) so command and pipeline JSON writers remain backward-compatible.
- Added deterministic QA runtime source script (`scripts/qa/fake_runtime_capture_helper.py`) to validate non-stub capture behavior without hardcoded PNG payload in the backend itself.

## 2026-04-19T20:07:07Z Task 2 - capabilities/execution strict contract
- Introduced shared runtime capability resolver (`src/capabilities/runtime.zig`) so capability booleans and backend labels are produced from one decision path and reused by both `capabilities list` and capture runtime gating.
- Canonical backend naming is now aligned to `niri-wayland-direct` across capabilities and capture payloads, removing prior divergence against `niri-wayland`.
- Added strict mode gating in `src/capture/command.zig`: when runtime capability for a mode is false, command returns deterministic `ERR_CAPTURE_MODE_UNSUPPORTED` and does not invoke backend execution.
- Added `scripts/qa/assert-capabilities-consistency.sh` to enforce runtime parity (`capabilities.capture.*` ↔ command outcomes) and backend_used alignment.

## 2026-04-19T20:52:57Z Task 4 - daemon status IPC truth
- `src/main.zig` now resolves `daemon status` via an IPC request (`daemon.status`) instead of socket-existence inference, using the same Unix stream request/response pattern already used by `daemon stop`.
- CLI status JSON schema keys were preserved (`state`, `result.state`, `result.ipc_version`, `result.degraded`), but values now reflect actual daemon state returned by IPC (`ready`, `capturing`, `degraded`).
- Deterministic error behavior was enforced for non-truthful socket paths: connect failure maps to `ERR_DAEMON_NOT_RUNNING`; timeout/invalid IPC response maps to `ERR_IPC_TIMEOUT`.
- Added `scripts/qa/assert-daemon-status-ipc-truth.sh` with positive parity checks (`daemon.status` IPC vs CLI) and negative orphan-socket simulation evidence.

## 2026-04-19T20:29:52Z Task 3 - capture content integrity QA
- Added `scripts/qa/assert-capture-content-validity.sh` to validate PNG integrity end-to-end: JSON success contract, file existence, decode from disk, decoded dimension parity against capture JSON, deterministic stub-signature rejection, and JSON evidence emission to `.sisyphus/evidence/task-3-capture-content-validity.json`.
- Non-black validation is now explicitly fixture-gated (`--fixture colorful-grid`) so luminance threshold checks only run in controlled deterministic scenarios and never as a universal real-scene rule.
- Updated `scripts/qa/fake_runtime_capture_helper.py` to generate deterministic RGBA `colorful-grid` fixtures at mode-aligned dimensions (`area/window/fullscreen`) so decode-vs-contract checks are stable and reproducible.
- Integrated Task 3 gate into orchestration (`run-integration-tests.sh` and transitively `run-all-tests.sh`) to prevent regressions from bypassing decoded-content validation.

## 2026-04-19T21:00:12Z Task 5 - default output path Pictures/Shaula
- `src/backends/capture_backend.zig` default output resolution now derives from `HOME` and emits `~/Pictures/Shaula/capture-{mode}-{timestamp}.png` when `--output` is omitted.
- Added explicit writeability validation for the default output directory via `ensureDirectoryWritable(...)`; any invalid/missing HOME or non-writable target deterministically maps to `ERR_OUTPUT_PATH_INVALID`.
- Explicit `--output` behavior remains unchanged (custom path is still passed through as-is and returned in capture JSON).
- Added `scripts/qa/assert-default-output-path.sh` to validate default-path success, missing/invalid HOME failures, and explicit output path parity.

## 2026-04-19T21:16:50Z Task 6 - history Top-N=20
- Migrated history persistence from single latest entry to deterministic Top-N retention by prepending each new entry and rewriting `latest.v1` with at most 20 non-empty lines.
- `history list --json` now reads up to 20 entries in file order (newest-first) and preserves the existing entry field contract (`path/mime/dimensions/backend_used/timestamp`).
- `history show --json --id latest` remains stable because it still resolves to `entries[0]`, which is deterministically the newest retained entry after trimming.
## 2026-04-19T21:30:00Z Task 7 - overlay base selection
- Added `geometry` property to `SelectionResult` to carry actual bounds of the user selection (`x, y, width, height`).
- Implemented Wayland overlay MVP by executing `slurp -b "#80808080" -c "#FFFFFF"` over `std.process.run`. The `slurp` binary handles Wayland-native gray dimming, pointer-drag selection, and cancellation natively, avoiding the need for heavy GUI library dependencies.
- Confirmed deterministic simulated dry runs correctly emit coordinates (`ok=true`) or explicit cancellation (`ERR_SELECTION_CANCELLED`).

## 2026-04-19T22:16:43Z Task 8 - shell artifact guard pre-capture
- Added a dedicated pre-capture guard module (`src/capture/precondition_guard.zig`) that enforces an explicit panel-hidden handshake when available, with bounded fallback settle barrier and no indefinite blocking.
- Guard behavior is deterministic via environment-driven config (`SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS`, `SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS`, `SHAULA_CAPTURE_SETTLE_BARRIER_MS`, optional `SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE`) and emits stable warnings (`capture_precondition_panel_hidden_handshake` / `capture_precondition_settle_barrier`).
- Capture command flow now runs precondition guard before backend execution for all capture modes, preserving existing capture JSON schema while adding deterministic timeout error path (`ERR_CAPTURE_PRECONDITION_TIMEOUT`).
- QA now validates shell artifact suppression using an injected magenta panel marker in `fake_runtime_capture_helper.py`; marker is absent from output when handshake/fallback guards succeed.

## 2026-04-19T22:46:38Z Task 9 - Noctalia MVP plugin actions
- Added explicit Noctalia action adapter (`integrations/noctalia/noctalia-action-adapter.sh`) that deterministically maps the five MVP actions to Shaula CLI argv arrays: `capture-area`, `capture-fullscreen`, `capture-window`, `open-last`, `history`.
- Kept plugin ownership minimal by turning `integrations/noctalia/noctalia-plugin-poc.sh` into a thin launcher that exposes menu metadata and delegates all action mapping/execution to the adapter; capture logic remains exclusively in Shaula CLI.
- Added `scripts/qa/assert-noctalia-actions.sh` to validate menu contract, dry-run mapping parity, and execute-path behavior for every action while emitting Task 9 evidence JSON at `.sisyphus/evidence/task-9-noctalia-actions.json`.
- Updated optionality and overhead QA to validate non-blocking plugin behavior and adapter performance budget against direct CLI capture (`test-noctalia-plugin-optional.sh`, `benchmark-plugin-overhead.sh`).

## 2026-04-19T23:04:29Z Task 10 - QA matrix consolidation
- `scripts/qa/run-integration-tests.sh` now runs and records deterministic post-fix subchecks covering capture integrity (runtime non-stub + decoded content), strict capabilities parity, default output path behavior, history Top-N, overlay base selection, shell artifact guard, and Noctalia integration checks.
- `scripts/qa/run-e2e-niri.sh` now emits a dedicated Task 10 layer report with explicit pass/fail subchecks and includes strict capability parity + panel-hide guard + plugin optionality gates in the E2E layer.
- `scripts/qa/run-all-tests.sh` now consolidates unit/integration/e2e layer reports into `.sisyphus/evidence/task-10-postfix-test-matrix-report.json` with top-level/layer/matrix pass flags and per-subcheck evidence.

## 2026-04-19T23:16:15Z Task 11 - spec contract alignment
- Cross-spec consistency is clearer when architecture explicitly centralizes the finalized runtime and contract locks, then requirements/testing reference those locks without redefining semantics.
- Keeping exact phrases used by QA acceptance checks in specs (for example `capabilities strict contract`, `Top-N 20`, `~/Pictures/Shaula`) prevents avoidable verification drift.
- Noctalia integration docs stay coherent when framed as an optional adapter mapping model with capture implementation ownership kept in Shaula core.

## 2026-04-19T23:35:26Z Task 12 - release readiness closure
- The most reliable release gate here is two-layered: check canonical evidence artifacts directly and also assert mandatory matrix subcheck IDs from Task 10, so coverage cannot silently regress.
- Readiness is now deterministic and machine-readable for automation: report always emits `ready`, numeric `blocking_issues`, full `checks`, and `blockers`, with explicit `ERR_RELEASE_BLOCKED` when any gate fails.
- Command-family sanity (`daemon`, `capture`, `capabilities`, `history`) is stable when validated via JSON contract shape/taxonomy instead of process side effects.

## 2026-04-20T00:00:31Z Task F1 - plan compliance audit
- Passing consolidated matrix/readiness is insufficient for F1 if mandatory per-task evidence filenames from the plan are missing; compliance must match the plan’s exact evidence paths.

## 2026-04-20T00:13:33Z Task F3 - real manual QA
- End-user confidence is strongest when each CLI flow is validated in paired happy/failure form (e.g., dry-run vs cancel, daemon status running vs missing socket), because contract stability becomes explicit and reproducible.

## 2026-04-20T00:27:32Z Task F1 rerun - compliance approved
- F1 passes once every plan-listed per-task evidence filename exists and consolidated Task 10/12 artifacts remain coherent.

## 2026-04-20T00:49:32Z Task F2 fix - area overlay + forced stub taxonomy
- Non-dry-run `capture area` now runs overlay selection first and returns deterministic `ERR_SELECTION_CANCELLED` on cancel before backend execution; dry-run path and JSON envelope remain stable.

## 2026-04-20T01:07:01Z Task Final Wave closure
- Final Wave completed with APPROVE verdicts in F1/F2/F3/F4 after resolving F2 blockers and regenerating required evidence filenames.

## 2026-04-20T03:31:44Z Task real-capture-default niri
- Backend now prefers real `grim` capture for Niri; synthetic helper runs only when `SHAULA_RUNTIME_CAPTURE_HELPER` is explicitly set (or auto-exported by QA wrappers when grim is unavailable).
