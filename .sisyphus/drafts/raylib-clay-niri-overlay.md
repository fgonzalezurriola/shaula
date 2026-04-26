# Draft: Raylib + Clay Niri Overlay Integration

## Requirements (confirmed)
- [user request]: "planea una integración Raylib+Clay"
- [user request]: "queremos lograr esto" (referencia visual estilo CleanShot X)
- [context]: Wayland-first, Niri-focused

## Technical Decisions
- [overlay topology]: helper overlay process invoked by `capture area` (internal subcommand), not in-process UI loop
- [fallback policy]: on Raylib/Clay init/render/input failure, fallback to existing slurp path unless explicit hard-fail flag is enabled
- [contract stability]: existing `capture area/fullscreen/window` JSON envelope remains unchanged
- [testability]: add deterministic overlay simulation hooks for agent-executable QA (no manual verification)
- [performance]: enforce interactive overlay startup/first-paint budgets with dedicated benchmark (no dry-run proxy)

## Research Findings
- [current flow]: `src/main.zig` -> `src/capture/command.zig:runArea` -> `src/overlay/overlay.zig:runSelection` -> `src/backends/capture_backend.zig:execute`
- [current overlay]: interactive path shells out to `slurp`; dry-run and cancel are deterministic in `src/overlay/overlay.zig`
- [build/test hooks]: `build.zig` builds from `src/main.zig`; tests load via `src/test_root.zig`
- [qa/perf]: integration/e2e contracts live in `scripts/qa/run-integration-tests.sh`, `scripts/qa/run-e2e-niri.sh`, `scripts/qa/run-all-tests.sh`, plus overlay perf benchmark script
- [external risk]: Raylib Wayland startup/stacking caveats require explicit probe+timeout+fallback guardrails

## Open Questions
- [none blocking]: defaults applied in plan for MVP scope and fallback behavior

## Scope Boundaries
- INCLUDE: plan de integración técnica y ejecución por tareas agent-executable
- EXCLUDE: implementación de código en esta fase (solo planificación)
