## [2026-04-20T14:14:17Z] Task: 1
- Unresolved: real Raylib+Clay runtime viability on live Niri compositor still needs dedicated dependency wiring and actual window/render loop validation in follow-up tasks.
- Unresolved: `ERR_OVERLAY_UNAVAILABLE` is currently spike-local; global taxonomy and recovery policy integration is deferred to planned Task 4.

## [2026-04-20T14:35:02Z] Task: 2
- No unresolved blockers in Task 2 scope; parser, tests, spec update, and evidence generation completed with deterministic behavior.

## [2026-04-20T14:51:12Z] Task: 3
- Unresolved: repository still lacks concrete  Raylib/Clay package entries; enabling  will require adding those dependencies in a future task.

## [2026-04-20T14:53:13Z] Task: 3
- No new unresolved blockers beyond missing real build.zig.zon Raylib/Clay dependency declarations for future real UI-link path.

## [2026-04-20T15:08:23Z] Task: 4
- No unresolved blockers remain for Task 4 scope.

## [2026-04-20T15:15:39Z] Task: 4 (fix)
- No unresolved blockers remain after QA deterministic env hardening.

## [2026-04-20T15:25:58Z] Task: 5
- Orchestrating cross-process IPC without leaving zombie windows will require strict cleanup logic once the real Niri/Wayland sockets are passed down.

## [2026-04-20T16:00:00Z] Task: 6
- Still utilizing UI dependency stubs in `build.zig`, preventing visual verification in CI environments without `build.zig.zon` resolution. Needs actual bindings to verify real overlay execution.
- Dummy PNG evidence is essentially empty; real pixel capture will require hooking `raylib.TakeScreenshot` in subsequent tasks.

## [2026-04-20T16:30:00Z] Task: 6 (fix-2)
- Although the PNG is correctly sized and displays the regions explicitly (background, frame, toolbar, dropdown, OCR/Record items), it is a static artifact used only in stub mode rather than a dynamic capture of the live `raylib` buffer. Dynamic rendering capture via `raylib.TakeScreenshot` remains deferred until display dependencies are real.
## [2026-04-20T15:57:48Z] Task: 7
- No unresolved blockers in Task 7 scope after implementing deterministic drag/resize/confirm/cancel state transitions and validating required evidence outputs.

## [2026-04-20T16:15:31Z] Task: 8
- No unresolved blocker for Task 8 integration scope; helper-first + configured fallback is now implemented and verified.

## [2026-04-20T16:26:44Z] Task: 9
- No unresolved blockers in Task 9 scope after contract-preservation and deterministic-cancel validation.

## [2026-04-20T16:40:25Z] Task: 10
- No unresolved blockers in Task 10 scope; Noctalia action hooks for open-folder/open-clipboard are optional, deterministic, and verified non-blocking for capture completion.

## [2026-04-20T17:03:02Z] Task: 11
- No unresolved blockers in Task 11 scope after adding deterministic interactive helper lanes and validating integration/e2e/build/test passes.

## [2026-04-20T17:28:32Z] Task: 11 (fix)
- No unresolved blockers in Task 11 fix scope after validating interactive suite, integration suite, headless e2e run, and `zig build`/`zig build test` all pass.

## [2026-04-20T18:17:31Z] Task: 12
- No unresolved blockers in Task 12 scope after enforcing interactive overlay latency gate and deterministic strict-threshold failure artifacts.

## [2026-04-20T18:30:06Z] Task: 12 (non-intrusive policy)
- No unresolved blockers in scope; non-intrusive default policy and intrusive opt-in flow are both implemented and verified in script-level QA/perf lanes.

## [2026-04-20T18:39:55Z] Task: qa-non-intrusive-policy
- No unresolved blockers in scope: intrusive interactive checks remain available behind explicit opt-in (`SHAULA_QA_ALLOW_INTRUSIVE_UI=1`) and are skipped/degraded deterministically by default.
