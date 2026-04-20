## 2026-04-18T20:10:24Z Task: session-bootstrap
- No blocking issues yet.

## 2026-04-18T20:15:31Z Task: task-1-zig-pin
- No blocking issues during implementation.
- Forced mismatch scenario intentionally failed with deterministic output token: `ERR_TOOLCHAIN_VERSION_MISMATCH`.
- Verification commands run exactly:
  - `bash scripts/qa/check-zig-version.sh`
  - `SHAULA_EXPECTED_ZIG=0.15.2 bash scripts/qa/check-zig-version.sh`
  - `zig version | grep -q '^0.16.0$'`
  - `test -f .tool-versions && grep -q '^zig 0.16.0$' .tool-versions`
  - `test -f scripts/qa/check-zig-version.sh && bash scripts/qa/check-zig-version.sh`
  - `grep -q 'Zig 0.16.0' spec/algo.md`

## 2026-04-18T20:31:00Z Task: task-2-niri-wayland-capability-matrix
- No blocking implementation issues.
- Expected failure scenario on fixture was validated and produced deterministic token `ERR_CAPABILITY_MATRIX_INCOMPLETE`.
- Observed tooling limitation: LSP diagnostics is not configured for `.md` in this environment, so markdown lint/diagnostics could not be executed via LSP for the new spec file.
- Validation commands run:
  - `bash scripts/qa/validate-capability-matrix.sh spec/wayland-niri-integration.md`
  - `bash scripts/qa/validate-capability-matrix.sh tests/fixtures/spec/wayland-integration-missing-matrix.md`

## 2026-04-18T21:14:16Z Task: task-3-cli-agent-first-contract
- No blocking implementation issues.
- Initial negative fixture attempt failed earlier than expected (`ERR_CLI_CONTRACT_INVALID`) due to missing machine-first policy marker; fixture was corrected to include policy while still omitting error taxonomy.
- Final negative scenario now deterministically emits required token: `ERR_CLI_ERROR_TAXONOMY_INCOMPLETE`.
- Validation commands run:
  - `bash scripts/qa/validate-cli-contract.sh spec/architecture.md spec/contracts/cli-schema.json`
  - `bash scripts/qa/validate-cli-contract.sh tests/fixtures/spec/architecture-missing-error-codes.md spec/contracts/cli-schema.json`

## 2026-04-18T22:03:00Z Task: task-4-architecture-process-topology-ipc-v1
- No blocking implementation issues.
- Negative fixture validation failed as expected with deterministic token `ERR_PLUGIN_HARD_DEPENDENCY`.
- Observed tooling limitation remains: LSP diagnostics is not configured for `.md`, so markdown diagnostics could not be executed via LSP for updated/fixture architecture files.
- Validation commands run:
  - `bash scripts/qa/validate-architecture-constraints.sh spec/architecture.md`
  - `bash scripts/qa/validate-architecture-constraints.sh tests/fixtures/spec/architecture-plugin-hard-dependency.md`
## [2026-04-18T18:06:27] Task: task-5-repo-bootstrap
- Sin problemas detectados durante el bootstrap del repositorio. El flujo de validación determinística funciona según lo esperado.

## [2026-04-19T00:15:01Z] Task: task-6-daemon-lifecycle
- Root cause of previous failure: std.Io Unix connect path surfaced unexpected errno 111 in this environment when probing daemon readiness before socket acceptance; this produced non-deterministic panic traces.
- Mitigation applied: removed startup readiness dependence on IPC connect and switched to deterministic socket presence + process liveness checks, eliminating the crash path for Task 6 acceptance workflow.

## [2026-04-19T00:31:16Z] Task: task-7-preflight
- Initial implementation issue: `parseFlags` accepted mutable argv type and failed Zig compile with const-mismatch; fixed signature to `[]const [*:0]const u8`.
- No blocking runtime issues after type fix; deterministic unsupported compositor negative path validated with `ERR_UNSUPPORTED_COMPOSITOR` and non-zero exit code.
- Observed expected readiness behavior: preflight can return `ok=true` with `ipc.ready=false` when daemon socket does not exist yet; this is intentional for runtime exposure rather than forced daemon lifecycle mutation.

## [$(date -u +"%Y-%m-%dT%H:%M:%SZ")] Task: task-8-overlay-latency
- Bash scripts simulating error path must handle `set -e` carefully because CLI error envelopes return non-zero exit codes.

## [$(date -u +"%Y-%m-%dT%H:%M:%SZ")] Task: task-8-overlay-latency
- Initial JSON output structure for selection properties mismatched the strictly enforced top-level acceptance tests, requiring a minor schema update.

## [2026-04-19T01:27:10Z] Task: task-9-capture-core
- Acceptance command in plan for area mode (`.ok==true and .mode=="area" and .backend_used|length>0`) is jq-precedence-sensitive; robust equivalent used during verification: `.ok==true and .mode=="area" and (.backend_used|length>0)`.
- Window unresolved-target path intentionally returns non-zero exit code with a valid JSON error envelope. QA scripts and evidence capture must handle this with `set +e` around command execution.
- No blocking implementation issues after resolving Zig 0.16 stdlib API differences (`ArrayList` allocator-aware methods and signatures).

## [2026-04-19T02:21:10Z] Task: task-10-output-pipeline
- Initial implementation attempted `std.fs` and `std.process` helpers not present in this Zig 0.16 environment (`std.fs.cwd`, `std.fs.readFileAbsoluteAlloc`, `std.process.getEnvVarOwned`), causing compile failures.
- Resolution: migrated file I/O to `std.Io.Dir` APIs (`createDirPath`, `createFileAbsolute`, `readFileAlloc`) and used `std.process.Environ`-based env reads for deterministic runtime behavior.
- History storage currently follows MVP deterministic minimal policy (single latest entry persisted at `/tmp/shaula/history/latest.v1`), sufficient for `history list --json` acceptance and consistency checks.
- Clipboard unavailable scenario is intentionally validated with `SHAULA_CLIPBOARD_AVAILABLE=0`; QA/evidence paths keep non-destructive deterministic behavior while preserving saved artifact success.

## [2026-04-19T03:10:00Z] Task: task-11-performance-gates
- `set -euo pipefail` combined with expected negative-path gate failures requires explicit capture patterns (`set +e` wrappers or redirected stderr files) when producing evidence for failure scenarios.
- Daemon idle measurement cannot rely only on CLI contract JSON to obtain PID; implementation resolved this by matching spawned `_serve` process commandline with unique per-run socket path and validating process liveness before sampling.
- Current implementation in this environment reports very low idle CPU (~0.0% avg) and RSS (~4.9MB p95), comfortably under default budgets (`cpu<=0.5`, `rss<=40MB`).

## [2026-04-19T03:20:00Z] Task: task-12-failure-matrix
- Initial failure-matrix run timed out because `_serve` was invoked with a socket path that still allowed bind/accept, causing a long-lived daemon loop instead of deterministic bind failure.
- Resolved by forcing an invalid bind target in QA (`--socket /dev/null/daemon.sock`) so daemon `_serve` fails immediately with `ERR_IPC_BIND_FAILED` and mapped exit code.
- Initial preflight expected-failure check depended on ambient `WAYLAND_DISPLAY`; made deterministic by unsetting it (`env -u WAYLAND_DISPLAY ...`) in both QA scripts.

## 2026-04-19T04:15:00Z Task: task-13-algo-spec
- No significant blockers; however, maintaining synchronization between `spec/algo.md` and other specialized specs (`architecture.md`, `performance.md`) requires manual discipline until more advanced doc-linting is introduced.

## 2026-04-19T04:30:00Z Task: task-13-algo-spec (Correction)
- Initial draft of `spec/algo.md` used relaxed performance budgets. Corrected to match hard-gate criteria from Task 11 and Plan.

### Task 14: Testing Bash UI
- When testing bash scripts calling bash wrappers, `set -e` causes premature exit if the UI script correctly returns a non-zero exit code on failure. Adding `|| true` ensures the test captures the output without crashing.

## 2026-04-19T04:02:34Z Task: task-15-noctalia-plugin
- Initial plugin QA run failed with `ERR_NOCTALIA_IPC_NON_OK_RESPONSE`; root cause was daemon `_serve` rejecting valid JSON IPC requests due to request parsing implementation detail in `src/daemon/server.zig`.
- Issue resolved by parsing request payload via daemon allocator (stable across parse/deinit lifecycle), restoring valid `daemon.status` IPC responses for plugin PoC flow.
- Deterministic negative evidence for Task 15 is produced via usage error path (`--unknown-flag`) in `benchmark-plugin-overhead.sh`, emitting `ERR_PLUGIN_BENCH_USAGE` with non-zero exit code.
### Validation Scripting
- Shell-based markdown table parsing is fragile; used `awk` with specific row skipping to handle standard markdown tables.
- Standardizing the Go/No-Go metric as a mandatory column prevents speculative "feature creep" without performance validation.

## 2026-04-19T04:22:42Z Task: task-17-test-matrix
- `spec/testing.md` and `spec/performance.md` were referenced by task requirements, but neither existed in this repository state before this task; created `spec/testing.md` and aligned with existing `spec/algo.md`/`spec/architecture.md` contracts.
- Real preflight readiness depends on actual daemon socket state (`result.ipc_ready=true`), so E2E requires a prior daemon start in this environment and hard-fails with `ERR_PREFLIGHT_ENV_NOT_READY` when not ready.
- LSP diagnostics are unavailable for `.txt` files in this environment; deterministic content checks were still validated through script execution and artifact inspection.
### Task 18 Issues
- Encountered missing 'performance.md' which was expected by the plan but not present in the workspace. Created it from scratch.

## 2026-04-19T05:27:47Z Task: final-wave-blocker-alignment
- During validation, parallel execution of history-related QA scripts caused intermittent `latest_path_mismatch` because both scripts mutate the shared single-entry history file (`/tmp/shaula/history/latest.v1`).
- Resolved by running targeted blocker QA sequentially in one deterministic command chain; behavior is stable with no contract regressions.
- One compile break introduced while reshaping history envelope (`missing opening {`) was fixed by replacing a malformed format string with a literal `writeAll` tail for JSON closure.

## 2026-04-19T13:16:34Z Task: task-16-future-feature-matrix-final-wave-fix
- Regression cause confirmed: global row parsing in the validator became fragile after adding a second table (`## MVP Capability Matrix`) in `spec/requirements.md`.
- Negative fixture (`tests/fixtures/spec/invalid-feature-matrix.md`) lacks the required section header and now deterministically fails with `ERR_FUTURE_FEATURE_GATE_MISSING` via section-scoped parsing.
- No additional blockers observed after fix; positive validation against `spec/requirements.md spec/phases.md` passes with deterministic output.

## 2026-04-19T13:34:18Z Task: final-wave-blockers-repro-fix
- Initial preflight-script attempt only injected a temporary socket path but did not create a listening daemon, so `.result.ipc_ready` remained false; fixed by spawning `shaula daemon _serve --json --socket <temp>` directly and waiting for socket readiness before the preflight call.
- `daemon start`/`daemon stop` cannot guarantee process cleanup for preflight QA because `daemon stop` removes only the socket file and may leave a detached `_serve` process; script now uses direct `_serve` with PID-based cleanup to avoid leaks.

## 2026-04-19T13:51:21Z Task: F3-real-manual-qa-unspecified-high
- REJECT blocker found in daemon lifecycle contract: `shaula daemon stop --json` reports `stopped=true`, but the daemon `_serve` process remains alive.
- Deterministic reproduction:
  1) `SOCK=$(mktemp -u "/tmp/shaula-f3-repro.XXXXXX.sock")`
  2) `SHAULA_SOCKET="$SOCK" ./zig-out/bin/shaula daemon start --json`
  3) `PID=$(pgrep -f "shaula daemon _serve --json --socket $SOCK" | head -n 1)`
  4) `SHAULA_SOCKET="$SOCK" ./zig-out/bin/shaula daemon stop --json`
  5) `kill -0 "$PID" && echo alive`
- Observed output snippet from QA run: `ALIVE_AFTER_STOP=true` with stop JSON showing success (`"command":"daemon stop","stopped":true`).
- User-visible impact: lifecycle contract drift (reported stopped while process still running), leading to leaked background daemon processes across repeated QA/test cycles.

## 2026-04-19T13:47:13Z Task: F1-plan-compliance-audit-oracle
- No compliance blockers found in audited scope (Tasks 1-18 + required Final Wave blocker regressions).
- Operational note (non-blocking): preflight and e2e verification remain environment-dependent on valid `WAYLAND_DISPLAY`, valid Unix `NIRI_SOCKET`, and `jq` availability; current audit environment satisfied all prerequisites.

## 2026-04-19T13:49:07Z Task: F4-scope-fidelity-deep
- Scope violation found: capture runtime currently succeeds on non-Niri compositor context, which expands beyond strict Niri-only v1 guardrail.
  - Evidence command: `SHAULA_COMPOSITOR=sway ./zig-out/bin/shaula capture area --json` returned `ok=true` with `backend_used="portal-screenshot"`.
  - Source evidence:
    - `src/backends/capture_backend.zig:107-113` falls back to `.portal_screenshot` when compositor is not Niri.
    - `src/capture/command.zig:79-88` executes capture backend directly without a Niri-only guard.
- Contradiction introduced: docs assert Niri-first-only scope while runtime permits non-Niri capture fallback success for `capture` commands.

## 2026-04-19T13:51:23Z Task: F2-code-quality-review
- BLOCKER (runtime robustness): daemon IPC malformed payload with non-string `command` crashes process (`panic: access of union field 'string' while field 'integer' is active` at `src/daemon/server.zig:81`). Repro command used: Python socket harness sending `{"command":123}` to `shaula daemon _serve --json --socket /tmp/shaula-f2-ipc.sock`; observed daemon exit `RC=-6` with no deterministic JSON error envelope.
- BLOCKER (lifecycle determinism): `shaula daemon stop --json --socket /tmp/shaula-f2-stop.sock` returns `stopped=true` while `_serve` PID remains alive. Repro command used: Python harness (start -> detect PID -> stop -> liveness check) produced `SERVE_PID_ALIVE_AFTER_STOP True`.
- MEDIUM (contract consistency): alias error path `shaula capabilities --bogus --json` returns `command="capabilities"` while success path canonicalizes to `command="capabilities list"`; this weakens canonical-command stability for agent parsers.
- MEDIUM (QA gate strictness): `scripts/qa/run-all-tests.sh` appends `ERR_PREFLIGHT_ENV_NOT_READY reason=negative_preflight_token_missing` but does not fail fast in that branch (`scripts/qa/run-all-tests.sh:27-29`), allowing potential false-pass on missing deterministic token checks.

## 2026-04-19T14:20:05Z Task: final-verification-wave-blockers-fix
- During combined long repro command execution, stale daemon `_serve` processes from previous runs caused intermittent Unix-socket connect `Errno 22` and timeout behavior; switched to isolated per-check commands with explicit socket readiness waits and cleanup traps.
- `run-all-tests.sh` negative-token simulation initially failed due to temporary-script `ROOT_DIR` resolution assumptions when copied outside repo path; fixed simulation harness by pinning temporary script `ROOT_DIR` to repository absolute path.

## 2026-04-19T14:25:56Z Task: F3-real-manual-qa-rerun-after-blocker-fix
- No blocking issues found in this rerun.
- Validation-note: daemon IPC error envelope is transport-level (`ipc_version/request_id/ok/error`) and does not include a CLI `command` field; malformed IPC checks should assert `error.code` + daemon survival, not CLI envelope shape.

## 2026-04-19T14:27:47Z Task: F2-code-quality-rereview-post-blocker-fix
- No blocking regressions found in the previous F2 reject scope after command-backed rerun and explicit reproductions.
- Non-blocking observation: runtime contains several `catch {}` transitions in daemon/state paths (intentional degrade behavior); current gate evidence shows deterministic outcomes and no crash regressions in tested paths.

## 2026-04-19T14:29:06Z Task: F4-scope-fidelity-deep-rerun
- Hard guardrail breach remains for AGENT-FIRST deterministic/machine-parseable output: JSON envelopes are string-interpolated without escaping dynamic path fields, producing invalid JSON when values contain quotes.
  - Repro evidence:
    - `SHAULA_COMPOSITOR=niri ./zig-out/bin/shaula capture area --json --output '/tmp/shaula/bad"q".png'` => exit 0, but `jq` parse fails (`jq rc=5`).
    - `SHAULA_COMPOSITOR=niri ./zig-out/bin/shaula capture area --json --save --output '/tmp/shaula/hist"q".png'` then `./zig-out/bin/shaula history list --json` => exit 0, but `jq` parse fails (`jq rc=5`).
  - Source locations:
    - `src/capture/command.zig:317,323,331`
    - `src/pipeline/post_capture.zig:89,96,104,110,117`
    - `src/history/command.zig:74-75,99-100`
- Contract drift issue (spec/runtime mismatch) affecting AGENT-FIRST contract fidelity:
  - Spec documents `--timeout-ms` for capture commands: `spec/architecture.md:41-43`.
  - Runtime rejects that flag as unsupported: `src/capture/command.zig:127-173` (unsupported path at `171-172`), command evidence `./zig-out/bin/shaula capture area --json --timeout-ms 100` => `ERR_CLI_USAGE`.

## 2026-04-19T14:29:43Z Task: F2-code-quality-rereview-final-evidence
- No blocker-level regressions observed in previously rejected F2 findings after explicit reproductions and full required command rerun.

## 2026-04-19T14:31:02Z Task: F1-plan-compliance-audit-rerun-oracle
- No compliance blockers found in current repository state for F1 gate scope.
- Non-blocking environment dependency reminder: preflight/e2e checks require valid `WAYLAND_DISPLAY`, Unix `NIRI_SOCKET`, and `jq`; all were present in this rerun.

## 2026-04-19T14:32:56Z Task: F4-finalize-current-state
- Scope-fidelity reject condition persists for AGENT-FIRST deterministic machine-parseable output:
  - `SHAULA_COMPOSITOR=niri ./zig-out/bin/shaula capture area --json --output '/tmp/shaula/f4"quote".png'` exits 0 but emits invalid JSON (`jq rc=5`).
  - `SHAULA_COMPOSITOR=niri ./zig-out/bin/shaula capture area --json --save --output '/tmp/shaula/f4hist"quote".png'` then `./zig-out/bin/shaula history list --json` exits 0 but emits invalid JSON (`jq rc=5`).
  - Source lines interpolate unescaped dynamic strings directly into JSON:
    - `src/capture/command.zig:317,323,331`
    - `src/pipeline/post_capture.zig:89,96,104,110,117`
    - `src/history/command.zig:74-75,99-100`
- Contract drift remains between documented capture grammar and runtime behavior:
  - Spec lists `--timeout-ms` on capture commands at `spec/architecture.md:41-43`.
  - Runtime rejects `--timeout-ms` as unsupported in capture flag parsing (`src/capture/command.zig:127-173`, unsupported at `171-172`), evidence: `./zig-out/bin/shaula capture area --json --timeout-ms 50` => `ERR_CLI_USAGE`, RC=2.

## 2026-04-19T14:39:02Z Task: f4-json-escaping-timeout-drift-fix
- No blocking issues after applying quote-safe JSON stringification.
- Verification note: timeout drift resolution intentionally chose spec alignment (remove unsupported `--timeout-ms` from capture grammar) to avoid introducing new runtime behavior beyond blocker scope.

## 2026-04-19T14:42:36Z Task: F4-recheck-after-blocker-fixes
- No blocking F4 scope-fidelity issues observed in this recheck.
- Operational note (non-blocking): `history list --json` reflects latest persisted entry from previous QA flows; this does not impact parseability or scope guardrails.

## 2026-04-19T14:48:25Z Task: F4-verdict-correction-after-background-results
- Verdict correction: prior APPROVE is superseded by direct failing evidence (current state is REJECT).
- Failing command evidence:
  - `./zig-out/bin/shaula clipboard copy-image --json --input '/tmp/shaula/clip"q".png' | jq -e .` -> jq parse error.
  - `./zig-out/bin/shaula 'bad"fam' --json | jq -e .` -> jq parse error.
  - `SHAULA_COMPOSITOR=niri NIRI_SOCKET=/tmp/niri.sock WAYLAND_DISPLAY=wayland-1 ./zig-out/bin/shaula preflight --json --socket '/tmp/qa"sock".sock' | jq -e .` -> jq parse error.
  - `./zig-out/bin/shaula capture area --json --dry-run | jq -e 'has("result")'` -> `false` (schema requires `result`).
- Source evidence:
  - Unescaped dynamic JSON fields: `src/clipboard/command.zig:87,113`; `src/main.zig:354`; `src/preflight/probe.zig:34,87`.
  - Dry-run success missing required `result`: `src/capture/command.zig:373` vs schema `spec/contracts/cli-schema.json:88-90`.
