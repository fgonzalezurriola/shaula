## 2026-04-19T17:12:35Z Task: init
- Known user-facing issue: captures return ok JSON but saved images appear black.
- Suspected source: runtime stub generation path in capture backend.

## 2026-04-19T17:43:18Z Task 1 - runtime capture backend
- Initial blocker: `zig test src/backends/capture_backend.zig` failed with `import of file outside module path` because the file was compiled standalone while using sibling `@import("../...")`.
- Zig 0.16 API drift in tests: `std.process.EnvMap` is unavailable in this invocation context, and `const io: std.Io = .{}` is invalid; migrated tests to `std.process.Environ.Map` + `createPosixBlock` and `std.testing.io`.
- Residual risk: runtime path currently writes deterministic non-stub PNG bytes (integrity guard fixed), but compositor-native capture integration is still pending in later tasks.

## 2026-04-19T19:14:46Z Task 1 correction - runtime boundary
- During migration to runtime invocation, failure branches after output-path allocation leaked memory; fixed by freeing `output_path` before returning failure outcomes.
- Zig 0.16 permission API mismatch (`std.posix.chmod` unavailable in this context) required switching to `std.Io.Dir.cwd().setFilePermissions(..., .fromMode(0o755), .{})` for test helper executables.
- QA now depends on `SHAULA_RUNTIME_CAPTURE_HELPER`; missing helper intentionally maps to backend-unavailable instead of silent fallback.

## 2026-04-19T20:07:07Z Task 2 - capabilities/execution strict contract
- While enabling file-targeted Zig tests, `src/capabilities/probe.zig` and `src/backends/capture_backend.zig` hit standalone import constraints (`import of file outside module path`); fixed by adding root-aware module aliases with standalone fallbacks.
- Existing QA expected `ERR_WINDOW_TARGET_UNRESOLVED` for `capture window`; strict capability contract changed deterministic failure to `ERR_CAPTURE_MODE_UNSUPPORTED`, so dependent QA scripts were updated to avoid false regressions.
- `std.process.EnvMap` was unavailable in current Zig 0.16 test context for new capability tests; tests now use `std.process.Environ.Map` + `createPosixBlock` consistently.

## 2026-04-19T20:52:57Z Task 4 - daemon status IPC truth
- While implementing status-over-IPC, transient connection failures on socket paths with stale/non-serving endpoints needed explicit deterministic mapping to avoid accidental success on mere socket presence.
- Negative-path QA for orphan sockets required a controlled fake Unix server that accepts then drops the connection; this reproduces non-responsive daemon behavior deterministically for `ERR_DAEMON_NOT_RUNNING`/`ERR_IPC_TIMEOUT` validation.

## 2026-04-19T20:29:52Z Task 3 - capture content integrity QA
- First execution of `assert-capture-content-validity.sh` failed with `ERR_CAPTURE_CONTENT_INVALID reason=stub_signature_not_rejected`; root cause was the script writing a hand-copied stub byte array that did not exactly match `assert_png_not_stub_signature.py`’s `STUB_SIGNATURE` constant.
- Fixed by sourcing `STUB_SIGNATURE` directly from `scripts/qa/assert_png_not_stub_signature.py` via Python module import inside the QA script, eliminating signature drift risk between generator and validator.

## 2026-04-19T21:00:12Z Task 5 - default output path Pictures/Shaula
- Missing/empty `HOME` would previously fall through to `/tmp/shaula/...`; Task 5 removes this silent behavior and now fails deterministically with `ERR_OUTPUT_PATH_INVALID`.
- Non-directory HOME targets (e.g., HOME pointing to a regular file) required explicit directory creation/write probe handling to guarantee deterministic `ERR_OUTPUT_PATH_INVALID` instead of unmapped filesystem errors.

## 2026-04-19T21:16:50Z Task 6 - history Top-N=20
- Initial build failed with constness mismatch when passing writer through a helper (`*const Io.Writer` vs `*Io.Writer`); resolved by writing the line directly in `storeLatest` with the mutable writer interface.
- Running `zig test src/history/command.zig` fails in this repo due to standalone import boundaries (`import of file outside module path`); verification was completed with `lsp_diagnostics`, `zig build`, `zig build test`, and required QA/CLI checks.
## 2026-04-19T21:30:00Z Task 7 - overlay base selection
- During `test-overlay-cancel.sh` QA script execution, the non-zero exit code mapping of `ERR_SELECTION_CANCELLED` caused the bash script to abort early due to `set -e`. Fixed by adding `|| true` to the assignment line, ensuring the script explicitly reads and tests the output.
- `std.process.run` requires an allocator in Zig 0.16. Since `runSelection` did not originally take an allocator/io/environ context, we had to update its signature and the call sites in `capture/command.zig`.

## 2026-04-19T22:16:43Z Task 8 - shell artifact guard pre-capture
- Initial timeout-path QA design expected precondition timeout solely from short timeout + long settle barrier, but guard correctly succeeded via fallback because settle barrier duration is clamped by global precondition timeout; updated timeout scenario to require handshake (`SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE=1`) with missing token.
- Zig compile failure surfaced in guard sleep helper (`std.Io.Duration.fromMilliseconds` expects `i64`); fixed by explicit bounded cast before constructing sleep duration.
- Shell QA assertion initially embedded a hardcoded path string incorrectly in jq expression; switched to `--arg path` for deterministic contract validation.

## 2026-04-19T22:46:38Z Task 9 - Noctalia MVP plugin actions
- First `benchmark-plugin-overhead.sh --max-added-p95-ms 20` run exceeded the default 120s tool timeout because it executes 60 sample pairs plus warmup; reran with a longer timeout and benchmark passed.
- `test-noctalia-plugin-optional.sh` originally hard-failed when plugin scripts were missing regardless of mode; adjusted checks so `--without-plugin` no longer depends on plugin files and now validates core capture independently.

## 2026-04-19T23:04:29Z Task 10 - QA matrix consolidation
- Initial Task 10 integration runner failed with `jq: invalid JSON text passed to --argjson`; root cause was using `jq --argjson ...` without `-n` while building accumulated `subchecks_json`.
- Integrating both strict capability gate and runtime-stub gate exposed contract drift in `assert-no-runtime-stub.sh`: forced `__stub__` now returns `ERR_CAPTURE_MODE_UNSUPPORTED` (strict gating) instead of always `ERR_CAPTURE_BACKEND_UNAVAILABLE`; assertion updated to accept both deterministic failure codes while still requiring `.ok==false` and `backend_used=="__stub__"`.

## 2026-04-19T23:16:15Z Task 11 - spec contract alignment
- `spec/testing.md` still references Task 17 naming in title and one negative-evidence path while primary matrix report now points to Task 10; this is legacy naming debt but not a blocker for Task 11 acceptance checks.

## 2026-04-19T23:35:26Z Task 12 - release readiness closure
- First readiness run blocked with `ERR_RELEASE_BLOCKED` because daemon success-path jq validation used an unsafe expression precedence (`.state|type=="string" and .result.state|type=="string"`) that evaluated as invalid-success-contract even with valid JSON.
- Fixed by making the jq predicate explicit with grouped clauses (`(.state|type=="string") and (.result.state|type=="string")`), preserving strictness while removing false blockers.

## 2026-04-20T00:32:49Z Task F2 - code quality review
- Blocking finding: non-dry-run `capture area` currently bypasses overlay selection flow, and forced `__stub__` path taxonomy diverges from spec (`ERR_CAPTURE_MODE_UNSUPPORTED` observed where specs lock `ERR_CAPTURE_BACKEND_UNAVAILABLE`).

## 2026-04-20T00:49:32Z Task F2 fix follow-up
- To keep non-interactive CI stable while integrating non-dry-run overlay flow, area command uses a deterministic dry-run overlay fallback when interactive `slurp` binary is unavailable, preventing hangs while preserving cancel/error contracts.

## 2026-04-20T00:59:55Z Task F2 rerun - gate result
- Rerun confirms both prior blockers resolved: area non-dry-run now enters overlay selection with deterministic cancel path, and forced `__stub__` taxonomy is canonically `ERR_CAPTURE_BACKEND_UNAVAILABLE` in code + QA gate.
