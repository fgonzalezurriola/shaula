# Shaula — Niri/Wayland Instant Capture Architecture & Spec Plan

## TL;DR
> **Summary**: Definir e implementar una base técnica Niri-first en Zig 0.16.0 con arquitectura daemon-first, CLI AGENT-FIRST y UI desacoplada para que hotkey→overlay se sienta instantáneo, con roadmap explícito para capacidades tipo CleanShot X sin comprometer el hot path.
> **Deliverables**:
> - `spec/algo.md` (documento central ejecutable)
> - `spec/requirements.md`
> - `spec/architecture.md`
> - `spec/wayland-niri-integration.md`
> - `spec/performance.md`
> - `spec/testing.md`
> - `spec/phases.md`
> - CLI contract AGENT-FIRST + test harness Niri
> **Effort**: XL
> **Parallel**: YES - 4 waves
> **Critical Path**: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 11 → 17 → 18

## Context
### Original Request
Diseñar desde cero Shaula (Linux screenshot app) enfocada en Niri + Wayland, escrita en Zig, con UX instantánea y arquitectura lista para crecer; investigar fuentes primarias, decidir versión Zig, estrategia Wayland/Niri/Noctalia, diseñar fases, pruebas y documentación técnica completa.

### Interview Summary
- Repo actual está en estado greenfield (sin código base ni CI/tooling).
- Decisiones confirmadas por usuario:
  - Compatibilidad v1: **Solo Niri estricto**.
  - Noctalia Shell plugin: **opcional post-MVP**.
  - Test strategy: **tests-after + quality gates**.
  - CLI en spec: **AGENT-FIRST** (determinística, automatizable por agentes) y UI consumiendo sólo caminos ya validados.

### Metis Review (gaps addressed)
Metis identificó guardrails obligatorios que quedan incorporados en este plan:
- Congelar límite MVP con capability matrix explícita.
- Definir contrato único CLI/daemon como source of truth.
- Separar plugin Noctalia del critical path.
- Definir spike Niri con criterios de salida go/no-go antes de consolidar MVP.
- Exigir criterios de aceptación 100% ejecutables por agente (sin validación manual).

## Work Objectives
### Core Objective
Entregar una base de producto técnicamente sólida para Shaula que consiga latencia percibida “instantánea” en Niri/Wayland, con arquitectura de procesos desacoplada y expandible para capacidades avanzadas tipo CleanShot X.

### Deliverables
1. Decisión de toolchain: Zig `0.16.0` pinneado y reproducible.
2. Arquitectura capability-driven con daemon residente, backend de captura por capacidades, overlay/UI desacoplada y plugin Noctalia opcional.
3. Contrato CLI AGENT-FIRST (estable, versionado, JSON determinístico, códigos de error explícitos).
4. Especificación completa en `spec/` (archivos listados en TL;DR).
5. Plan de pruebas unit/integration/e2e con entorno Niri real para validación crítica.
6. Fases con objetivos, riesgos, criterios de aceptación y tests por fase.
7. Registro de riesgos/dependencias y go/no-go criteria para features avanzadas.

### Definition of Done (verifiable conditions with commands)
- `test -f spec/algo.md && test -f spec/requirements.md && test -f spec/architecture.md && test -f spec/wayland-niri-integration.md && test -f spec/performance.md && test -f spec/testing.md && test -f spec/phases.md`
- `grep -q "Decision Register" spec/algo.md`
- `grep -q "MVP Capability Matrix" spec/requirements.md`
- `grep -q "AGENT-FIRST CLI Contract" spec/architecture.md`
- `grep -q "Niri Spike Exit Criteria" spec/wayland-niri-integration.md`
- `grep -q "Latency SLO" spec/performance.md`
- `grep -q "Preflight QA Gate" spec/testing.md`
- `grep -q "Phase 0" spec/phases.md && grep -q "Hardening" spec/phases.md && grep -q "Noctalia" spec/phases.md`

### Must Have
- Niri-first estricto en v1 (sin scope cross-compositor en MVP).
- Hot path optimizado por latencia (hotkey→overlay, capture completion).
- Degradación explícita por capacidad no disponible, sin fallos silenciosos.
- Toda interfaz UI apoyada en rutas ya validadas por CLI/backend.
- Criterios de aceptación y QA totalmente automatizables por agentes.

### Must NOT Have (guardrails, AI slop patterns, scope boundaries)
- No acoplar core de captura a plugin Noctalia.
- No prometer scrolling capture estable en MVP sin data objetiva.
- No usar validaciones manuales como criterio de cierre.
- No introducir capas arquitectónicas sin motivación de performance/fiabilidad medible.
- No expandir alcance v1 a portabilidad general Wayland/X11.

## Verification Strategy
> ZERO HUMAN INTERVENTION - all verification is agent-executed.
- Test decision: **tests-after** con `zig test` (unit/integration) + harness e2e en entorno Niri.
- QA policy: Cada tarea incluye escenarios happy + edge/failure con evidencia.
- Evidence: `.sisyphus/evidence/task-{N}-{slug}.{ext}`
- Preflight obligatorio: comando único de readiness del entorno Niri antes de e2e de captura.

## Execution Strategy
### Parallel Execution Waves
> Target: 5-8 tasks per wave. <3 per wave (except final) = under-splitting.
> Extract shared dependencies as Wave-1 tasks for max parallelism.

Wave 1: Foundation & decision locks (Tasks 1-5)
- Toolchain pin/reproducibility
- Niri spike + capability matrix + limits
- AGENT-FIRST CLI contract and state model
- Repo skeleton + spec skeleton + QA harness baseline

Wave 2: Core capture vertical slice (Tasks 6-10)
- Daemon core + backend abstraction
- Overlay and selection UX path
- Fullscreen/area/window capture on Niri
- Clipboard/save/history minimal deterministic pipeline

Wave 3: Hardening, performance, and comprehensive specs (Tasks 11-15)
- Latency instrumentation + benchmarks + gates
- Failure handling/recovery matrix
- Complete technical docs in `spec/`
- MVP UI shell-agnostic on top of validated CLI
- Noctalia plugin design/PoC (optional post-MVP)

Wave 4: Forward architecture + full QA matrix + readiness (Tasks 16-18)
- Advanced features architecture contracts (not full implementation)
- Niri e2e automation hardening
- Phase plan + risk register + release readiness packet

### Dependency Matrix (full, all tasks)
| Task | Depends On | Blocks |
|---|---|---|
| 1 | - | 2,3,4,5,6,11,17 |
| 2 | 1 | 4,6,7,8,9,12,17 |
| 3 | 1,2 | 4,5,6,8,9,10,14,17 |
| 4 | 1,2,3 | 5,6,7,8,9,10,14 |
| 5 | 1,3,4 | 6,7,8,9,10 |
| 6 | 1,2,3,4,5 | 7,8,9,10,11,12,17 |
| 7 | 6 | 9,10,12,17 |
| 8 | 4,5,6 | 9,11,17 |
| 9 | 6,7,8 | 10,11,12,14,17 |
| 10 | 4,6,7,9 | 11,12,14,17 |
| 11 | 1,6,8,9,10 | 17,18 |
| 12 | 2,6,7,9,10,11 | 17,18 |
| 13 | 2,3,4,5,6,7,8,9,10,11,12 | 18 |
| 14 | 4,5,9,10 | 17,18 |
| 15 | 4,5,9,10,11 | 18 |
| 16 | 2,4,5,11,12,13 | 18 |
| 17 | 1,2,3,6,7,8,9,10,11,12,14 | 18 |
| 18 | 11,12,13,14,15,16,17 | F1-F4 |

### Agent Dispatch Summary (wave → task count → categories)
- Wave 1 → 5 tasks → `deep`, `writing`, `unspecified-high`
- Wave 2 → 5 tasks → `deep`, `ultrabrain`, `unspecified-high`
- Wave 3 → 5 tasks → `deep`, `writing`, `unspecified-high`
- Wave 4 → 3 tasks → `deep`, `writing`, `unspecified-high`
- Final Verification → 4 tasks → `oracle`, `deep`, `unspecified-high`

## TODOs
> Implementation + Test = ONE task. Never separate.
> EVERY task MUST have: Agent Profile + Parallelization + QA Scenarios.

<!-- TASK_DETAILS_INSERT_BEFORE_FINAL_VERIFICATION -->

- [x] 1. Pin de Zig 0.16.0 + reproducibilidad de build

  **What to do**: Definir política de toolchain para Shaula con pin explícito a Zig `0.16.0`, validación automática en CI/local y documentación de upgrade path (`0.16.x` patch-only en MVP). Incluir archivos de pin (`.tool-versions` o equivalente), script de verificación y sección en `spec/algo.md`.
  **Must NOT do**: No usar Zig nightly/master en camino principal; no dejar versión “implícita” dependiente de máquina local.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: decisión de base que impacta todo el proyecto y su estabilidad.
  - Skills: `[]` - no skill especializada requerida para este alcance.
  - Omitted: `[frontend-design]` - no aplica a setup de toolchain/backend.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 2,3,4,5,6,11,17 | Blocked By: none

  **References** (executor has NO interview context - be exhaustive):
  - Baseline: `AGENTS.md:1` - confirma identidad/nombre del proyecto Shaula.
  - External: `https://ziglang.org/download/` - versiones estables oficiales.
  - External: `https://ziglang.org/documentation/master/#Build-System` - guía build system para reproducibilidad.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `zig version | grep -q '^0.16.0$'`
  - [ ] `test -f .tool-versions && grep -q '^zig 0.16.0$' .tool-versions`
  - [ ] `test -f scripts/qa/check-zig-version.sh && bash scripts/qa/check-zig-version.sh`
  - [ ] `grep -q 'Zig 0.16.0' spec/algo.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Toolchain correcta
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/check-zig-version.sh`
    Expected: Exit code 0 y salida incluye `zig=0.16.0`
    Evidence: .sisyphus/evidence/task-1-zig-pin.txt

  Scenario: Toolchain incorrecta
    Tool: Bash
    Steps: Ejecutar `SHAULA_EXPECTED_ZIG=0.15.2 bash scripts/qa/check-zig-version.sh`
    Expected: Exit code != 0 y mensaje `ERR_TOOLCHAIN_VERSION_MISMATCH`
    Evidence: .sisyphus/evidence/task-1-zig-pin-error.txt
  ```

  **Commit**: YES | Message: `chore(toolchain): pin zig 0.16.0 for reproducible builds` | Files: `.tool-versions`, `scripts/qa/check-zig-version.sh`, `spec/algo.md`

- [x] 2. Spike técnico Niri/Wayland + capability matrix verificable

  **What to do**: Ejecutar fase de investigación técnica aplicada y consolidar en `spec/wayland-niri-integration.md` una matriz de capacidades reales (area/fullscreen/window, permisos, overlay/input focus, limitaciones de scrolling/record/OCR). Incluir decisión explícita de estrategia primaria Niri y fallback policy (sin expandir scope cross-compositor v1).
  **Must NOT do**: No asumir soporte de protocolos sin evidencia; no declarar features “listas” si quedan inciertas.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: requiere análisis de protocolos, constraints de compositor y validación rigurosa.
  - Skills: `[]` - investigación guiada por fuentes primarias.
  - Omitted: `[uncodixfy]` - no aplica a investigación de backend/protocolos.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 4,6,7,8,9,12,17 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - External: `https://github.com/niri-wm/niri` - fuente oficial del compositor objetivo.
  - External: `https://github.com/niri-wm/niri/pull/3731` - evidencia de screenshot tile semantics.
  - External: `https://wayland.app/protocols/wlr-layer-shell-unstable-v1` - base para overlay surfaces.
  - External: `https://wayland.app/protocols/wlr-screencopy-unstable-v1` - referencia de captura legacy/fallback.
  - External: `https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html` - portal screenshot semantics.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -f spec/wayland-niri-integration.md`
  - [ ] `grep -q 'Niri Spike Exit Criteria' spec/wayland-niri-integration.md`
  - [ ] `grep -q 'MVP Capability Matrix' spec/wayland-niri-integration.md`
  - [ ] `grep -q 'Uncertainties' spec/wayland-niri-integration.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Matriz completa y accionable
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-capability-matrix.sh spec/wayland-niri-integration.md`
    Expected: Exit code 0 y validación de columnas: feature, protocolo, estado, fallback, riesgo
    Evidence: .sisyphus/evidence/task-2-capability-matrix.txt

  Scenario: Gap crítico de evidencia
    Tool: Bash
    Steps: Ejecutar validador sobre fixture incompleto `tests/fixtures/spec/wayland-integration-missing-matrix.md`
    Expected: Exit code != 0 y mensaje `ERR_CAPABILITY_MATRIX_INCOMPLETE`
    Evidence: .sisyphus/evidence/task-2-capability-matrix-error.txt
  ```

  **Commit**: YES | Message: `docs(wayland): add niri spike findings and capability matrix` | Files: `spec/wayland-niri-integration.md`, `scripts/qa/validate-capability-matrix.sh`, `tests/fixtures/spec/*`

- [x] 3. Definir contrato CLI AGENT-FIRST (source of truth)

  **What to do**: Diseñar contrato CLI versionado y determinístico (`shaula ...`) para que agentes puedan ejecutar y testear sin UI: comandos de captura (`area/fullscreen/window`), daemon (`start/status/stop`), capabilities, clipboard, history, output JSON estable y códigos de error explícitos. Documentar gramática + ejemplos + esquema JSON.
  **Must NOT do**: No crear CLI orientada a humanos con salida no parseable; no introducir acciones UI-only sin equivalente CLI.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: contrato de interfaz estable con impacto en backend/UI/tests.
  - Skills: `[]` - sin skill especializada obligatoria.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 4,5,6,8,9,10,14,17 | Blocked By: 1,2

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md:24-28` - decisión AGENT-FIRST.
  - External: `https://clig.dev/` - principios CLI robusta (adaptar a modo machine-first).
  - External: `https://www.json.org/json-en.html` - formato de salida estable.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -f spec/architecture.md && grep -q 'AGENT-FIRST CLI Contract' spec/architecture.md`
  - [ ] `test -f spec/contracts/cli-schema.json`
  - [ ] `bash scripts/qa/validate-cli-contract.sh spec/architecture.md spec/contracts/cli-schema.json`
  - [ ] `grep -q 'ERR_' spec/architecture.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Contrato parseable por agente
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-cli-contract.sh spec/architecture.md spec/contracts/cli-schema.json`
    Expected: Exit code 0 y confirmación de comandos mínimos: capture, daemon, capabilities, history, clipboard
    Evidence: .sisyphus/evidence/task-3-cli-contract.txt

  Scenario: Comando sin error code definido
    Tool: Bash
    Steps: Validar fixture `tests/fixtures/spec/architecture-missing-error-codes.md`
    Expected: Exit code != 0 y mensaje `ERR_CLI_ERROR_TAXONOMY_INCOMPLETE`
    Evidence: .sisyphus/evidence/task-3-cli-contract-error.txt
  ```

  **Commit**: YES | Message: `docs(cli): define agent-first contract and json schema` | Files: `spec/architecture.md`, `spec/contracts/cli-schema.json`, `scripts/qa/validate-cli-contract.sh`

- [x] 4. Definir arquitectura de procesos + IPC versionado

  **What to do**: Especificar arquitectura completa: daemon residente (core), proceso overlay, backend de captura, worker(s) asíncronos, interfaz UI y plugin Noctalia opcional. Definir IPC (socket path, versioning, timeouts, retries, health checks), state machine y reglas de degradación.
  **Must NOT do**: No convertir plugin Noctalia en dependencia dura; no mezclar hot path con tareas pesadas (OCR/encoding largo).

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: diseño de sistema multi-proceso y contrato operacional.
  - Skills: `[]` - no skill específica.
  - Omitted: `[uncodixfy]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 5,6,7,8,9,10,14 | Blocked By: 1,2,3

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md:75-81` - política de verificación y preflight.
  - External: `https://man7.org/linux/man-pages/man7/unix.7.html` - Unix domain sockets para IPC local.
  - External: `https://docs.noctalia.dev/development/plugins/api/` - límites y superficie del plugin opcional.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `grep -q 'Process Topology' spec/architecture.md`
  - [ ] `grep -q 'IPC Contract v1' spec/architecture.md`
  - [ ] `grep -q 'Plugin Optionality Rule' spec/architecture.md`
  - [ ] `bash scripts/qa/validate-architecture-constraints.sh spec/architecture.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Arquitectura cumple guardrails críticos
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-architecture-constraints.sh spec/architecture.md`
    Expected: Exit code 0; validadores confirman daemon source-of-truth y plugin opcional
    Evidence: .sisyphus/evidence/task-4-architecture-topology.txt

  Scenario: Acoplamiento indebido al plugin
    Tool: Bash
    Steps: Validar fixture `tests/fixtures/spec/architecture-plugin-hard-dependency.md`
    Expected: Exit code != 0 y mensaje `ERR_PLUGIN_HARD_DEPENDENCY`
    Evidence: .sisyphus/evidence/task-4-architecture-topology-error.txt
  ```

  **Commit**: YES | Message: `docs(architecture): define daemon-first topology and ipc v1` | Files: `spec/architecture.md`, `scripts/qa/validate-architecture-constraints.sh`, `tests/fixtures/spec/*`

- [x] 5. Bootstrap del repositorio + estructura operacional mínima

  **What to do**: Crear estructura inicial del proyecto para ejecución por agentes: `src/`, `spec/`, `scripts/qa/`, `tests/`, `.sisyphus/evidence/`, y documentación base (`README`, `CONTRIBUTING` técnico). Incluir convenciones de naming, layout de módulos y rutas de artefactos de QA.
  **Must NOT do**: No mezclar lógica de features en esta tarea; no crear estructura ambigua sin convención explícita.

  **Recommended Agent Profile**:
  - Category: `[quick]` - Reason: bootstrap estructural directo con decisiones ya tomadas.
  - Skills: `[]` - no skill adicional necesaria.
  - Omitted: `[design-taste-frontend]` - no hay UI a diseñar aquí.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 6,7,8,9,10 | Blocked By: 1,3,4

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md:42-50` - deliverables esperados.
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md:75-81` - política de evidencia y preflight.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -d src && test -d spec && test -d scripts/qa && test -d tests && test -d .sisyphus/evidence`
  - [ ] `test -f README.md && test -f CONTRIBUTING.md`
  - [ ] `grep -q '.sisyphus/evidence' CONTRIBUTING.md`
  - [ ] `bash scripts/qa/preflight-repo-structure.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Estructura mínima correcta
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/preflight-repo-structure.sh`
    Expected: Exit code 0 y reporte de rutas requeridas existentes
    Evidence: .sisyphus/evidence/task-5-repo-bootstrap.txt

  Scenario: Carpeta requerida ausente
    Tool: Bash
    Steps: Ejecutar validador contra fixture sin `spec/`
    Expected: Exit code != 0 y mensaje `ERR_REPO_STRUCTURE_INVALID`
    Evidence: .sisyphus/evidence/task-5-repo-bootstrap-error.txt
  ```

  **Commit**: YES | Message: `chore(repo): bootstrap project layout and qa evidence paths` | Files: `README.md`, `CONTRIBUTING.md`, `scripts/qa/preflight-repo-structure.sh`, `src/*`, `tests/*`

- [x] 6. Implementar daemon core residente + state machine de backend

  **What to do**: Implementar daemon de larga vida en Zig con ciclo de vida explícito (`initializing`, `ready`, `capturing`, `degraded`, `error`), socket IPC local y health endpoint/command. Priorizar tiempos de wake-up y path corto hotkey→capture.
  **Must NOT do**: No bloquear event loop por operaciones pesadas; no cargar OCR/recording en arranque del daemon.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: componente central de runtime y fiabilidad.
  - Skills: `[]` - sin skill externa obligatoria.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 7,8,9,10,11,12,17 | Blocked By: 1,2,3,4,5

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/architecture.md` - Process Topology + IPC Contract v1 (Task 4).
  - Internal: `spec/wayland-niri-integration.md` - capability limits Niri/Wayland (Task 2).
  - Internal: `spec/architecture.md` - AGENT-FIRST CLI Contract (Task 3).
  - External: `https://man7.org/linux/man-pages/man7/unix.7.html` - IPC Unix socket.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `shaula daemon start --json | jq -e '.status=="ready"'`
  - [ ] `shaula daemon status --json | jq -e '.state|IN("ready","degraded")'`
  - [ ] `shaula daemon stop --json | jq -e '.stopped==true'`
  - [ ] `bash scripts/qa/assert-daemon-state-machine.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Ciclo start/status/stop estable
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-daemon-lifecycle.sh`
    Expected: Exit code 0; estados válidos y transición sin timeout
    Evidence: .sisyphus/evidence/task-6-daemon-lifecycle.txt

  Scenario: Socket ocupado o inválido
    Tool: Bash
    Steps: Forzar `SHAULA_SOCKET=/tmp/shaula-bad.sock` y lanzar daemon
    Expected: Exit code != 0; error `ERR_IPC_BIND_FAILED` y degradación controlada
    Evidence: .sisyphus/evidence/task-6-daemon-lifecycle-error.txt
  ```

  **Commit**: YES | Message: `feat(daemon): add resident core and explicit backend state machine` | Files: `src/daemon/*`, `src/ipc/*`, `scripts/qa/test-daemon-lifecycle.sh`, `scripts/qa/assert-daemon-state-machine.sh`

- [x] 7. Implementar preflight Niri + capability probing runtime

  **What to do**: Implementar comando `shaula preflight --json` y `shaula capabilities --json` que detecte compositor Niri, socket IPC disponible, permisos/superficies requeridas y backend capture path habilitable. Resultado debe alimentar decisiones runtime sin heurísticas ocultas.
  **Must NOT do**: No asumir Niri por variables incompletas; no ocultar capacidades faltantes.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: detección robusta de entorno y seguridad operativa.
  - Skills: `[]` - no skill extra.
  - Omitted: `[uncodixfy]` - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 9,10,12,17 | Blocked By: 2,4,6

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/wayland-niri-integration.md` - Niri Spike Exit Criteria + capability matrix.
  - External: `https://github.com/niri-wm/niri` - IPC/env expected behavior.
  - External: `https://flatpak.github.io/xdg-desktop-portal/docs/` - portal fallback semantics.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `shaula preflight --json | jq -e '.ok==true and .compositor=="niri"'`
  - [ ] `shaula capabilities --json | jq -e '.capture.area==true and .capture.fullscreen==true'`
  - [ ] `shaula capabilities --json | jq -e 'has("backend") and has("fallbacks")'`
  - [ ] `bash scripts/qa/assert-preflight-schema.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Entorno Niri válido
    Tool: Bash
    Steps: Ejecutar `shaula preflight --json` en sesión Niri de test
    Expected: `.ok=true`, `.compositor="niri"`, `.errors=[]`
    Evidence: .sisyphus/evidence/task-7-preflight.txt

  Scenario: Entorno inválido/no Niri
    Tool: Bash
    Steps: Ejecutar con variables de sesión simuladas no Niri
    Expected: Exit code != 0 y `ERR_UNSUPPORTED_COMPOSITOR`
    Evidence: .sisyphus/evidence/task-7-preflight-error.txt
  ```

  **Commit**: YES | Message: `feat(preflight): add niri capability probing and readiness checks` | Files: `src/preflight/*`, `src/capabilities/*`, `scripts/qa/assert-preflight-schema.sh`

- [x] 8. Implementar path de overlay/selección ultra-rápido

  **What to do**: Implementar overlay de selección (freeform + aspect-ratio presets iniciales) optimizado para latencia, acoplado al daemon por IPC liviano. Debe soportar cancelación (`Esc`), confirmación y coordenadas determinísticas en JSON.
  **Must NOT do**: No acoplar lógica de persistencia/clipboard al render loop del overlay; no permitir jitter visual > budget.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: mezcla de input handling, rendering y constraints Wayland.
  - Skills: `[]` - sin skill obligatoria.
  - Omitted: `[frontend-design]` - foco técnico de latencia, no estética.

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 9,11,17 | Blocked By: 4,5,6

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/performance.md` - Latency SLO.
  - Internal: `spec/architecture.md` - Process topology + IPC.
  - External: `https://wayland.app/protocols/wlr-layer-shell-unstable-v1` - overlay layers.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `shaula capture area --dry-run --json | jq -e '.selection.mode=="freeform"'`
  - [ ] `shaula capture area --aspect 16:9 --dry-run --json | jq -e '.selection.aspect=="16:9"'`
  - [ ] `bash scripts/qa/benchmark-overlay-first-paint.sh --p95-max-ms 75 --p99-max-ms 110`
  - [ ] `bash scripts/qa/test-overlay-cancel.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Overlay responde dentro de budget
    Tool: Bash
    Steps: Ejecutar benchmark `bash scripts/qa/benchmark-overlay-first-paint.sh --samples 100`
    Expected: p95 <= 75ms y p99 <= 110ms
    Evidence: .sisyphus/evidence/task-8-overlay-latency.json

  Scenario: Cancelación por ESC
    Tool: Bash
    Steps: Inyectar flujo de cancelación sobre overlay activo
    Expected: Estado final `cancelled`, sin artefacto parcial, exit code controlado
    Evidence: .sisyphus/evidence/task-8-overlay-latency-error.txt
  ```

  **Commit**: YES | Message: `feat(overlay): add low-latency region selection pipeline` | Files: `src/overlay/*`, `src/selection/*`, `scripts/qa/benchmark-overlay-first-paint.sh`, `scripts/qa/test-overlay-cancel.sh`

- [x] 9. Implementar backends de captura v1 (area/fullscreen/window) con degradación explícita

  **What to do**: Implementar backends de captura para v1 Niri-first con selección runtime por capacidades: ruta primaria Niri/Wayland directa y fallback controlado sólo cuando aplique. Cubrir `area`, `fullscreen`, `window` con salida uniforme (`path`, `mime`, `dimensions`, `backend_used`, `latency_ms`).
  **Must NOT do**: No esconder fallback automático sin reportarlo; no marcar `window` como exitoso si geometría/identidad es incierta (degradar a `area` explícitamente).

  **Recommended Agent Profile**:
  - Category: `[ultrabrain]` - Reason: núcleo técnico con múltiples rutas y edge cases de compositor.
  - Skills: `[]` - implementación directa sobre contratos definidos.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 10,11,12,14,17 | Blocked By: 6,7,8

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/wayland-niri-integration.md` - capability matrix + límites.
  - Internal: `spec/architecture.md` - backend interface + state machine.
  - External: `https://wayland.app/protocols/wlr-screencopy-unstable-v1` - captura.
  - External: `https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html` - fallback semantics.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `shaula capture area --json | jq -e '.ok==true and .mode=="area" and .backend_used|length>0'`
  - [ ] `shaula capture fullscreen --json | jq -e '.ok==true and .mode=="fullscreen"'`
  - [ ] `shaula capture window --json | jq -e '.mode=="window" and (has("ok"))'`
  - [ ] `bash scripts/qa/assert-capture-result-schema.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Captura area/fullscreen funcional
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-capture-core-modes.sh`
    Expected: Exit code 0; archivos creados con MIME image/png y dimensiones > 0
    Evidence: .sisyphus/evidence/task-9-capture-core.txt

  Scenario: Window capture incierto
    Tool: Bash
    Steps: Simular condición de ventana no resoluble y ejecutar `shaula capture window --json`
    Expected: `ok=false` o `degraded=true` con `ERR_WINDOW_TARGET_UNRESOLVED`
    Evidence: .sisyphus/evidence/task-9-capture-core-error.txt
  ```

  **Commit**: YES | Message: `feat(capture): add niri-first area fullscreen window backends` | Files: `src/capture/*`, `src/backends/*`, `scripts/qa/test-capture-core-modes.sh`, `scripts/qa/assert-capture-result-schema.sh`

- [x] 10. Pipeline determinístico de salida (save/clipboard/history)

  **What to do**: Implementar pipeline unificado post-captura: guardar archivo, copiar al clipboard, registrar historial y emitir evento estructurado para UI. Definir políticas de nombre, ruta por defecto, retención inicial y lectura de historial por CLI.
  **Must NOT do**: No duplicar lógica entre CLI y UI; no bloquear flujo por fallo secundario de clipboard si archivo ya fue persistido.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: path crítico de experiencia y consistencia de datos.
  - Skills: `[]` - sin skill adicional.
  - Omitted: `[uncodixfy]` - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 11,12,14,17 | Blocked By: 4,6,7,9

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/requirements.md` - MVP outputs esperados.
  - Internal: `spec/architecture.md` - contracts de history/clipboard.
  - External: `https://specifications.freedesktop.org/clipboard-spec/` - clipboard conventions (si aplican a toolkit elegido).

  **Acceptance Criteria** (agent-executable only):
  - [ ] `shaula capture area --copy --save --json | jq -e '.ok==true and .saved.path|length>0 and .clipboard.ok==true'`
  - [ ] `shaula history list --json | jq -e 'length>=1 and .[0].path|length>0'`
  - [ ] `shaula clipboard import-image --json | jq -e 'has("ok")'`
  - [ ] `bash scripts/qa/test-history-consistency.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Save + clipboard + history consistentes
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-post-capture-pipeline.sh`
    Expected: Exit code 0; hash archivo coincide con hash entrada en historial
    Evidence: .sisyphus/evidence/task-10-output-pipeline.txt

  Scenario: Falla clipboard sin perder captura
    Tool: Bash
    Steps: Simular backend clipboard no disponible y ejecutar captura con `--copy --save`
    Expected: `saved.ok=true`, `clipboard.ok=false`, error `ERR_CLIPBOARD_UNAVAILABLE`
    Evidence: .sisyphus/evidence/task-10-output-pipeline-error.txt
  ```

  **Commit**: YES | Message: `feat(pipeline): add deterministic save clipboard history flow` | Files: `src/pipeline/*`, `src/history/*`, `src/clipboard/*`, `scripts/qa/test-post-capture-pipeline.sh`

- [x] 11. Instrumentación y budgets de performance (hard gates)

  **What to do**: Instrumentar métricas de latencia del hot path y footprint del daemon; crear benchmark scripts reproducibles (cold/hot start, overlay paint, capture completion, idle CPU/RSS). Configurar quality gates que fallen al superar budgets.
  **Must NOT do**: No medir con muestras insuficientes (<30) ni sin aislar warmup; no aceptar métricas “aproximadas” sin artefacto.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: performance engineering con criterios cuantitativos.
  - Skills: `[]` - no skill externa.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 17,18 | Blocked By: 1,6,8,9,10

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/performance.md` - SLOs objetivo.
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md:163-171` - success criteria numéricos.
  - External: `https://man7.org/linux/man-pages/man1/time.1.html` - medición de tiempos/procesos.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/benchmark-overlay-first-paint.sh --p95-max-ms 75 --p99-max-ms 110`
  - [ ] `bash scripts/qa/benchmark-capture-completion.sh --area-p95-max-ms 150 --window-p95-max-ms 220`
  - [ ] `bash scripts/qa/benchmark-daemon-idle.sh --cpu-max 0.5 --rss-max-mb 40`
  - [ ] `test -f .sisyphus/evidence/task-11-performance-gates.json`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Budgets cumplidos
    Tool: Bash
    Steps: Ejecutar suite `bash scripts/qa/run-performance-gates.sh`
    Expected: Exit code 0; todas las métricas dentro de umbral
    Evidence: .sisyphus/evidence/task-11-performance-gates.json

  Scenario: Regresión de latencia
    Tool: Bash
    Steps: Ejecutar suite con umbral forzado (`--p95-max-ms 10`) para verificar gate
    Expected: Exit code != 0 y mensaje `ERR_PERF_BUDGET_EXCEEDED`
    Evidence: .sisyphus/evidence/task-11-performance-gates-error.txt
  ```

  **Commit**: YES | Message: `perf(qa): add latency and footprint gates for niri hot path` | Files: `scripts/qa/benchmark-*.sh`, `scripts/qa/run-performance-gates.sh`, `spec/performance.md`

- [x] 12. Manejo de fallos y degradación controlada

  **What to do**: Implementar y documentar taxonomía de errores (`ERR_*`), recovery policy y estados degradados para fallos de compositor, permisos, IPC, backend, clipboard, output path. Debe existir mapping determinístico error→acción→exit code.
  **Must NOT do**: No devolver errores genéricos sin contexto; no hacer retries infinitos.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: robustez operacional y resiliencia transversal.
  - Skills: `[]` - sin skill especializada.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 17,18 | Blocked By: 2,6,7,9,10,11

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/testing.md` - failure scenarios requeridos.
  - Internal: `spec/architecture.md` - state machine y IPC contract.
  - External: `https://man7.org/linux/man-pages/man3/errno.3.html` - referencia de error handling en Linux.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `grep -q 'Error Taxonomy' spec/architecture.md`
  - [ ] `shaula errors list --json | jq -e 'length>0 and .[0].code|startswith("ERR_")'`
  - [ ] `bash scripts/qa/test-failure-matrix.sh`
  - [ ] `bash scripts/qa/assert-exit-code-mapping.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Fallos manejados con degradación limpia
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-failure-matrix.sh`
    Expected: Exit code 0; cada caso produce código ERR_* esperado y recovery policy correcta
    Evidence: .sisyphus/evidence/task-12-failure-matrix.txt

  Scenario: Error no mapeado
    Tool: Bash
    Steps: Inyectar fallo sintético no catalogado
    Expected: Exit code != 0 y mensaje `ERR_UNKNOWN_UNMAPPED`
    Evidence: .sisyphus/evidence/task-12-failure-matrix-error.txt
  ```

  **Commit**: YES | Message: `fix(core): enforce explicit error taxonomy and degraded recovery` | Files: `src/errors/*`, `src/recovery/*`, `scripts/qa/test-failure-matrix.sh`, `scripts/qa/assert-exit-code-mapping.sh`

- [x] 13. Escribir `spec/algo.md` como documento central decision-complete

  **What to do**: Redactar `spec/algo.md` consolidando: resumen ejecutivo técnico, decisiones finales, Decision Register, MVP capability matrix, arquitectura de procesos, contrato CLI AGENT-FIRST, estrategia Wayland/Niri, performance budgets, pruebas, fases y riesgos.
  **Must NOT do**: No dejar placeholders ni secciones ambiguas; no duplicar información conflictiva respecto a docs satélite.

  **Recommended Agent Profile**:
  - Category: `[writing]` - Reason: síntesis técnica estructurada y accionable.
  - Skills: `[]` - sin skill adicional.
  - Omitted: `[analyze-logs]` - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 18 | Blocked By: 2,3,4,5,6,7,8,9,10,11,12

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/requirements.md`, `spec/architecture.md`, `spec/wayland-niri-integration.md`, `spec/performance.md`, `spec/testing.md`, `spec/phases.md`.
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md` (este plan completo).

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -f spec/algo.md`
  - [ ] `grep -q 'Decision Register' spec/algo.md`
  - [ ] `grep -q 'MVP Capability Matrix' spec/algo.md`
  - [ ] `grep -q 'AGENT-FIRST CLI' spec/algo.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Documento central completo
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-algo-spec.sh spec/algo.md`
    Expected: Exit code 0; todas las secciones obligatorias presentes
    Evidence: .sisyphus/evidence/task-13-algo-spec.txt

  Scenario: Sección crítica faltante
    Tool: Bash
    Steps: Validar fixture sin Decision Register
    Expected: Exit code != 0 y mensaje `ERR_ALGO_SPEC_MISSING_SECTION`
    Evidence: .sisyphus/evidence/task-13-algo-spec-error.txt
  ```

  **Commit**: YES | Message: `docs(spec): author central algo architecture blueprint` | Files: `spec/algo.md`, `scripts/qa/validate-algo-spec.sh`, `tests/fixtures/spec/*`

- [x] 14. Diseñar interfaz MVP mínima consumiendo sólo backend validado

  **What to do**: Implementar UI mínima (launcher/overlay controls/history quick list) que invoque exclusivamente contratos CLI/daemon ya validados. Incluir estados loading/error/degraded mapeados 1:1 a códigos backend.
  **Must NOT do**: No introducir lógica de captura directa en UI; no bifurcar comportamiento fuera del contrato AGENT-FIRST.

  **Recommended Agent Profile**:
  - Category: `[visual-engineering]` - Reason: capa de experiencia conectada a backend robusto.
  - Skills: `[]` - no skill obligatoria.
  - Omitted: `[redesign-existing-projects]` - foco es integración funcional MVP.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 17,18 | Blocked By: 4,5,9,10

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/architecture.md` - AGENT-FIRST CLI Contract.
  - Internal: `spec/testing.md` - UI must use validated backend paths.
  - Internal: `spec/performance.md` - latency limits for UI-triggered flows.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/test-ui-contract-adherence.sh`
  - [ ] `bash scripts/qa/test-ui-error-state-mapping.sh`
  - [ ] `bash scripts/qa/test-ui-capture-smoke.sh`
  - [ ] `grep -q 'UI Backend Contract Rule' spec/architecture.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: UI usa solo rutas validadas
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-ui-contract-adherence.sh`
    Expected: Exit code 0; no invocaciones fuera de comandos/IPC permitidos
    Evidence: .sisyphus/evidence/task-14-ui-contract.txt

  Scenario: Error backend propagado mal
    Tool: Bash
    Steps: Inyectar `ERR_CLIPBOARD_UNAVAILABLE` y validar mapping visual/estado
    Expected: Exit code != 0 en test si mapping no coincide con spec
    Evidence: .sisyphus/evidence/task-14-ui-contract-error.txt
  ```

  **Commit**: YES | Message: `feat(ui): add mvp shell-agnostic frontend on validated backend` | Files: `src/ui/*`, `scripts/qa/test-ui-*.sh`

- [x] 15. Evaluar integración Noctalia plugin (post-MVP opcional)

  **What to do**: Implementar PoC de plugin Noctalia no crítico: panel lateral/acciones rápidas conectadas por IPC versionado al daemon. Documentar compatibilidad, latencia añadida y aislamiento de fallos.
  **Must NOT do**: No mover funciones core de captura al plugin; no bloquear hot path cuando plugin no está presente.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: integración opcional con shell externo y riesgos de acoplamiento.
  - Skills: `[]` - sin skill obligatoria.
  - Omitted: `[frontend-design]` - prioridad es integración/latencia, no polish.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 18 | Blocked By: 4,5,9,10,11

  **References** (executor has NO interview context - be exhaustive):
  - External: `https://docs.noctalia.dev/development/plugins/manifest/`
  - External: `https://docs.noctalia.dev/development/plugins/api/`
  - External: `https://github.com/noctalia-dev/noctalia-shell/blob/4ebf258f/Services/Noctalia/PluginService.qml`
  - Internal: `spec/architecture.md` - Plugin Optionality Rule.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/test-noctalia-plugin-optional.sh`
  - [ ] `bash scripts/qa/benchmark-plugin-overhead.sh --max-added-p95-ms 15`
  - [ ] `grep -q 'Noctalia Optional Integration' spec/architecture.md`
  - [ ] `grep -q 'non-blocking' spec/wayland-niri-integration.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Plugin presente y funcional
    Tool: Bash
    Steps: Ejecutar suite `bash scripts/qa/test-noctalia-plugin-optional.sh --with-plugin`
    Expected: Exit code 0; acciones rápidas disparan comandos daemon válidos
    Evidence: .sisyphus/evidence/task-15-noctalia-plugin.txt

  Scenario: Plugin ausente
    Tool: Bash
    Steps: Ejecutar suite `bash scripts/qa/test-noctalia-plugin-optional.sh --without-plugin`
    Expected: Exit code 0; flujo de captura core intacto sin dependencia
    Evidence: .sisyphus/evidence/task-15-noctalia-plugin-error.txt
  ```

  **Commit**: YES | Message: `feat(noctalia): add optional plugin integration proof-of-concept` | Files: `integrations/noctalia/*`, `scripts/qa/test-noctalia-plugin-optional.sh`, `scripts/qa/benchmark-plugin-overhead.sh`

- [x] 16. Diseñar arquitectura futura de features tipo CleanShot X (sin sobrecargar MVP)

  **What to do**: Especificar contratos y fases para features futuras: all-in-one, pre-area flow, scrolling capture, self-timer, OCR, record screen, pin-to-screen, hide desktop icons policy, open from clipboard/file picker, presets/aspect ratio, advanced history. Cada feature debe incluir factibilidad, dependencias, riesgo y go/no-go metric.
  **Must NOT do**: No implementar features avanzadas fuera de fase; no prometer disponibilidad sin criterios medibles.

  **Recommended Agent Profile**:
  - Category: `[writing]` - Reason: diseño evolutivo y priorización técnica.
  - Skills: `[]` - no skill adicional requerida.
  - Omitted: `[frontend-design]` - documento técnico, no UI final.

  **Parallelization**: Can Parallel: YES | Wave 4 | Blocks: 18 | Blocked By: 2,4,5,11,12,13

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/requirements.md` - alcance MVP vs futuro.
  - Internal: `spec/phases.md` - orden de implementación.
  - Internal: `spec/performance.md` - restricciones de hot path.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `grep -q 'Future Feature Contracts' spec/requirements.md`
  - [ ] `grep -q 'Scrolling Capture: Experimental' spec/requirements.md`
  - [ ] `grep -q 'Go/No-Go' spec/phases.md`
  - [ ] `bash scripts/qa/validate-future-feature-matrix.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Matriz futura completa
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-future-feature-matrix.sh spec/requirements.md spec/phases.md`
    Expected: Exit code 0; todas las features futuras tienen factibilidad + dependencia + métrica
    Evidence: .sisyphus/evidence/task-16-future-features.txt

  Scenario: Feature sin go/no-go
    Tool: Bash
    Steps: Validar fixture con OCR sin criterio de salida
    Expected: Exit code != 0 y `ERR_FUTURE_FEATURE_GATE_MISSING`
    Evidence: .sisyphus/evidence/task-16-future-features-error.txt
  ```

  **Commit**: YES | Message: `docs(roadmap): define cleanshot-style future feature contracts` | Files: `spec/requirements.md`, `spec/phases.md`, `scripts/qa/validate-future-feature-matrix.sh`

- [x] 17. Construir plan de pruebas completo (unit/integration/e2e Niri real)

  **What to do**: Definir y automatizar estrategia de testing completa: tests sin compositor real (unit/mocks/contracts), integración con harness local y e2e en sesión Niri real para región/fullscreen/window, clipboard, fallos de permisos/compositor y estados backend.
  **Must NOT do**: No dejar escenarios críticos sólo manuales; no usar e2e sin preflight gate.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: diseño y ejecución de QA multinivel con entorno real Wayland.
  - Skills: `[]` - sin skill extra.
  - Omitted: `[frontend-design]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 4 | Blocks: 18 | Blocked By: 1,2,3,6,7,8,9,10,11,12,14

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/testing.md` - matriz de pruebas objetivo.
  - Internal: `spec/performance.md` - budgets medibles.
  - Internal: `spec/wayland-niri-integration.md` - constraints del entorno.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -f spec/testing.md && grep -q 'Preflight QA Gate' spec/testing.md`
  - [ ] `bash scripts/qa/preflight-wayland-niri.sh`
  - [ ] `bash scripts/qa/run-unit-tests.sh && bash scripts/qa/run-integration-tests.sh && bash scripts/qa/run-e2e-niri.sh`
  - [ ] `test -f .sisyphus/evidence/task-17-test-matrix-report.json`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Suite completa pasa en entorno Niri
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/run-all-tests.sh`
    Expected: Exit code 0; reporte final incluye unit/integration/e2e y cobertura de escenarios requeridos
    Evidence: .sisyphus/evidence/task-17-test-matrix-report.json

  Scenario: Preflight falla
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/preflight-wayland-niri.sh` en entorno no compatible
    Expected: Exit code != 0 y mensaje `ERR_PREFLIGHT_ENV_NOT_READY`
    Evidence: .sisyphus/evidence/task-17-test-matrix-report-error.txt
  ```

  **Commit**: YES | Message: `test(qa): add full niri wayland test strategy and automated gates` | Files: `spec/testing.md`, `scripts/qa/run-*.sh`, `tests/*`

- [x] 18. Consolidación final de documentación: fases, riesgos, dependencias y decisión técnica

  **What to do**: Completar paquete documental final en `spec/`: resumen ejecutivo, decisiones técnicas recomendadas (Zig, Wayland/Niri, arquitectura de proceso, Noctalia), fases detalladas, plan de pruebas, matriz de riesgos/dependencias y referencias. Alinear todo con requerimiento de salida del usuario.
  **Must NOT do**: No dejar contradicciones entre documentos; no cerrar sin lista de incertidumbres y verificación pendiente.

  **Recommended Agent Profile**:
  - Category: `[writing]` - Reason: consolidación integral multi-documento con consistencia técnica.
  - Skills: `[]` - no skill extra requerida.
  - Omitted: `[analyze-logs]` - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 4 | Blocks: F1-F4 | Blocked By: 11,12,13,14,15,16,17

  **References** (executor has NO interview context - be exhaustive):
  - Internal: `spec/algo.md`, `spec/requirements.md`, `spec/architecture.md`, `spec/wayland-niri-integration.md`, `spec/performance.md`, `spec/testing.md`, `spec/phases.md`.
  - Internal: `.sisyphus/plans/shaula-niri-wayland-instant-capture.md`.
  - External: enlaces primarios usados en investigación de Zig/Niri/Wayland/Noctalia.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `test -f spec/algo.md && test -f spec/requirements.md && test -f spec/architecture.md && test -f spec/wayland-niri-integration.md && test -f spec/performance.md && test -f spec/testing.md && test -f spec/phases.md`
  - [ ] `bash scripts/qa/validate-spec-cross-links.sh spec`
  - [ ] `bash scripts/qa/validate-spec-output-format.sh spec/algo.md`
  - [ ] `test -f .sisyphus/evidence/task-18-docs-readiness.txt`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Paquete documental completo y consistente
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-all-specs.sh`
    Expected: Exit code 0; no referencias rotas ni secciones faltantes
    Evidence: .sisyphus/evidence/task-18-docs-readiness.txt

  Scenario: Inconsistencia entre specs
    Tool: Bash
    Steps: Ejecutar validador sobre fixture con decisiones contradictorias
    Expected: Exit code != 0 y mensaje `ERR_SPEC_INCONSISTENT_DECISIONS`
    Evidence: .sisyphus/evidence/task-18-docs-readiness-error.txt
  ```

  **Commit**: YES | Message: `docs(spec): finalize architecture phases testing and risk package` | Files: `spec/*.md`, `scripts/qa/validate-*.sh`, `.sisyphus/evidence/*`

## Final Verification Wave (MANDATORY — after ALL implementation tasks)
> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.
> **Do NOT auto-proceed after verification. Wait for user's explicit approval before marking work complete.**
> **Never mark F1-F4 as checked before getting user's okay.** Rejection or user feedback -> fix -> re-run -> present again -> wait for okay.
- [x] F1. Plan Compliance Audit — oracle
- [x] F2. Code Quality Review — unspecified-high
- [x] F3. Real Manual QA — unspecified-high (+ playwright if UI)
- [x] F4. Scope Fidelity Check — deep

## Commit Strategy
- Commit por tarea cerrada o subgrupo cohesivo, evitando commits gigantes multi-tema.
- Convención: `type(scope): why-focused-description`
- Tipos preferidos: `feat`, `perf`, `fix`, `test`, `docs`, `chore`.
- Nunca mezclar cambios de arquitectura + plugin Noctalia en un mismo commit.
- Requerir evidencia asociada en `.sisyphus/evidence/` antes de merge.

## Success Criteria
- UX objetivo medido (Niri hot path):
  - hotkey → overlay first paint: p95 ≤ 75ms, p99 ≤ 110ms
  - selection confirm → capture available: p95 ≤ 150ms (area/fullscreen), p95 ≤ 220ms (window)
- Daemon footprint:
  - idle CPU ≤ 0.5% promedio
  - RSS ≤ 40MB
- Reliability:
  - success rate captura ≥ 99.0% en flujos soportados
  - crash-free sessions ≥ 99.5%
- Spec completada y coherente con decisiones v1 Niri-first + AGENT-FIRST CLI.
- Noctalia plugin permanece no crítico para captura en MVP.
