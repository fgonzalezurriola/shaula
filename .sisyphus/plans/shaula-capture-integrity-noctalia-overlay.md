# Shaula — Corrección de Captura Real + Overlay Base + Integración Noctalia MVP

## TL;DR
> **Summary**: Corregir de raíz el problema de imágenes negras reemplazando el backend stub por captura real, cerrar la brecha de QA con validación de contenido decodificado, y luego habilitar overlay base + menú Noctalia MVP sin contaminar capturas con animaciones de shell.
> **Deliverables**:
> - Backend de captura real (sin `writeStubPng` en runtime productivo)
> - Contrato estricto `capabilities ↔ execution` (sin inconsistencias en `window`)
> - Historial Top-N (20) + default output `~/Pictures/Shaula`
> - Overlay v1 (gris + selección base + confirmar/cancelar)
> - Plugin Noctalia MVP (area/fullscreen/window/open-last/history) con guard anti-artefactos
> - QA extendido con validación PNG decodificada + rechazo de firma stub
> **Effort**: Large
> **Parallel**: YES - 3 waves
> **Critical Path**: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12

## Context
### Original Request
El usuario reporta que el plan de 18 fases terminó en verde y los tests pasan, pero las imágenes en `/tmp` salen negras. Además solicita integración con Noctalia shell (botón/menú simple) y verificar la feature principal de overlay gris con selección de área tipo CleanShot X, evitando animaciones de control center en la captura.

### Interview Summary
- Prioridad confirmada: resolver primero la captura negra.
- Noctalia MVP confirmado: `Capture Area`, `Capture Fullscreen`, `Capture Window`, `Open Last`, `History`.
- Overlay v1 confirmado: selección base (gris + drag/resize + confirmar/cancelar), sin tooling avanzada.
- Historial confirmado: Top-N con tamaño por defecto 20.
- Regla de capacidades confirmada: estricta (si `capabilities.window=false`, no puede haber éxito en `capture window`).
- QA confirmado: validación fuerte (decode PNG real + non-black en fixture controlado + rechazo de firma stub).
- Ruta de salida default confirmada: `~/Pictures/Shaula`.

### Metis Review (gaps addressed)
- Guardrail aplicado: **capture correctness first** (fase inicial bloqueante), luego overlay y Noctalia.
- Guardrail aplicado: backend ID canónico único (sin `niri-wayland` vs `niri-wayland-direct` divergente).
- Guardrail aplicado: `capabilities` debe ser fuente de verdad operativa y consistente con ejecución.
- Guardrail aplicado: overlay v1 limitado a selección base (sin scope creep editor/anotaciones).
- Guardrail aplicado: Noctalia control-center widget mínimo; UI más rica en panel.
- Gap resuelto por default explícito: rechazo de firma stub en runtime; chequeo non-black queda en QA de fixture controlado para evitar falsos positivos en escenas oscuras reales.

## Work Objectives
### Core Objective
Eliminar salidas negras en captura real de Shaula con garantías verificables, manteniendo compatibilidad Niri/Wayland actual, y entregar una base operativa para overlay v1 + menú Noctalia MVP sin artefactos visuales de shell.

### Deliverables
1. Backend de captura productivo sin stub runtime.
2. Validación QA de integridad visual (PNG decodificado + firma stub bloqueada + fixture de contenido).
3. Contrato de capacidades estricto y consistente con comandos de captura.
4. Historial Top-N=20 con orden y recorte determinístico.
5. Resolución de output por defecto en `~/Pictures/Shaula` con manejo explícito de errores.
6. Overlay base funcional (gris, selección, confirmar/cancelar).
7. Integración Noctalia MVP opcional y no bloqueante.
8. Matriz E2E actualizada + evidencia en `.sisyphus/evidence/`.

### Definition of Done (verifiable conditions with commands)
- `bash scripts/qa/run-all-tests.sh`
- `bash scripts/qa/assert-capture-content-validity.sh`
- `bash scripts/qa/assert-capabilities-consistency.sh`
- `bash scripts/qa/assert-history-topn.sh --topn 20 --shots 25`
- `HOME=/tmp/shaula-home bash scripts/qa/assert-default-output-path.sh`
- `bash scripts/qa/assert-overlay-base-selection.sh`
- `bash scripts/qa/assert-noctalia-actions.sh`
- `./zig-out/bin/shaula capabilities list --json | jq -e '.capture.window == false or .capture.window == true' >/dev/null`

### Must Have
- Fix real de captura (no simulado) en ruta de producción.
- QA agent-executable sin intervención humana para validar contenido real.
- Coherencia total entre `capabilities` y comportamiento de `capture`.
- Historial Top-N=20 con orden newest-first estable.
- Noctalia opcional: la captura core debe funcionar sin plugin.

### Must NOT Have (guardrails, AI slop patterns, scope boundaries)
- No fallback silencioso a stub en runtime.
- No checks visuales manuales como criterio de cierre.
- No ampliar overlay v1 a editor/anotaciones/presets avanzados.
- No usar animaciones de control-center en el momento de captura.
- No expandir alcance a compositor no-Niri en este plan.

## Verification Strategy
> ZERO HUMAN INTERVENTION - all verification is agent-executed.
- Test decision: **tests-after** sobre QA existente + nuevos checks de integridad visual.
- QA policy: Cada tarea combina implementación + pruebas (happy + edge/failure).
- Evidence: `.sisyphus/evidence/task-{N}-{slug}.{ext}`.
- Reglas de evidencia crítica:
  - Captura no negra validada por decode PNG + fixture deterministic.
  - Firma stub explícitamente rechazada.
  - `capabilities` y `capture` comparados en script de consistencia.

## Execution Strategy
### Parallel Execution Waves
> Target: 5-8 tasks per wave. <3 per wave (except final) = under-splitting.
> Extract shared dependencies as Wave-1 tasks for max parallelism.

Wave 1: Capture integrity foundation (Tasks 1-4)
- Sustituir stub runtime por ruta real
- Canonicalizar backend/capabilities
- Fortalecer QA de contenido
- Corregir status/estado daemon en rutas críticas

Wave 2: Data/output consistency (Tasks 5-8)
- Default output `~/Pictures/Shaula`
- Historial Top-N=20
- Overlay base v1
- Guard anti-artefactos de shell

Wave 3: Noctalia MVP + end-to-end consolidation (Tasks 9-12)
- Menú/plugin Noctalia MVP
- QA integrado de acciones y artefactos
- Actualización de contratos/specs/scripts
- Consolidación final de matriz de verificación

### Dependency Matrix (full, all tasks)
| Task | Depends On | Blocks |
|---|---|---|
| 1 | - | 2,3,4,5,6,7,8,9,10,11,12 |
| 2 | 1 | 3,4,9,10,11,12 |
| 3 | 1,2 | 10,11,12 |
| 4 | 1,2 | 10,11,12 |
| 5 | 1 | 6,10,11,12 |
| 6 | 5 | 10,11,12 |
| 7 | 1,2,4 | 8,10,11,12 |
| 8 | 7 | 9,10,11,12 |
| 9 | 2,8 | 10,11,12 |
| 10 | 3,4,6,8,9 | 11,12 |
| 11 | 2,3,6,9,10 | 12 |
| 12 | 10,11 | F1,F2,F3,F4 |

### Agent Dispatch Summary (wave → task count → categories)
- Wave 1 → 4 tasks → `ultrabrain`, `deep`, `unspecified-high`
- Wave 2 → 4 tasks → `deep`, `unspecified-high`, `visual-engineering`
- Wave 3 → 4 tasks → `unspecified-high`, `writing`, `deep`

## TODOs
> Implementation + Test = ONE task. Never separate.
> EVERY task MUST have: Agent Profile + Parallelization + QA Scenarios.

<!-- TASK_DETAILS_INSERT_BEFORE_FINAL_VERIFICATION -->

- [x] 1. Sustituir backend stub por captura real de runtime

  **What to do**: Reemplazar la ruta productiva de `src/backends/capture_backend.zig` para que `execute(...)` no escriba PNG hardcodeado. Introducir separación explícita entre backend real y backend stub de test, con guard de compilación/runtime para impedir que el stub se use en modo usuario. Mantener formato de salida JSON existente (`mode/path/mime/dimensions/backend_used/latency_ms`) para no romper contrato CLI.
  **Must NOT do**: No dejar fallback silencioso al stub; no cambiar shape JSON de `capture` en esta tarea.

  **Recommended Agent Profile**:
  - Category: `[ultrabrain]` - Reason: cambio crítico en hot path de captura con riesgo alto de regresión.
  - Skills: [`zig-best-practices`] - reforzar patrones Zig 0.16.0 para seguridad de memoria y diseño modular.
  - Omitted: [`frontend-design`] - no aplica a backend core.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 2,3,4,5,6,7,8,9,10,11,12 | Blocked By: none

  **References** (executor has NO interview context - be exhaustive):
  - Pattern to replace: `src/backends/capture_backend.zig:88-98` - actualmente retorna éxito tras `writeStubPng`.
  - Stub implementation: `src/backends/capture_backend.zig:169-198` - bytes PNG hardcodeados 1x1.
  - Capture contract writer: `src/capture/command.zig:257-355` - shape de éxito/error JSON.
  - External: `https://niri-wm.github.io/niri/Screencasting.html` - referencia oficial de captura/screencast en Niri.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-no-runtime-stub.sh`
  - [ ] `./zig-out/bin/shaula capture area --json | jq -e '.ok==true and .mime=="image/png" and .path|length>0' >/dev/null`
  - [ ] `python3 scripts/qa/assert_png_not_stub_signature.py "$(./zig-out/bin/shaula capture area --json | jq -r '.path')"`
  - [ ] `zig test src/backends/capture_backend.zig`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Captura real sin firma stub
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-no-runtime-stub.sh`
    Expected: Exit code 0 y mensaje `ok capture_runtime_backend_non_stub`
    Evidence: .sisyphus/evidence/task-1-real-backend.txt

  Scenario: Forzar ruta inválida de backend
    Tool: Bash
    Steps: Ejecutar `SHAULA_CAPTURE_BACKEND=__stub__ ./zig-out/bin/shaula capture area --json`
    Expected: Exit code != 0 y error `ERR_CAPTURE_BACKEND_UNAVAILABLE` (sin archivo de salida)
    Evidence: .sisyphus/evidence/task-1-real-backend-error.txt
  ```

  **Commit**: YES | Message: `fix(capture): replace stub runtime path with real backend` | Files: `src/backends/capture_backend.zig`, `scripts/qa/assert-no-runtime-stub.sh`, `scripts/qa/assert_png_not_stub_signature.py`

- [x] 2. Unificar capacidades y ejecución con contrato estricto

  **What to do**: Crear una sola fuente de verdad para capacidades runtime y usarla tanto en `capabilities list` como en gating de `capture`. Corregir inconsistencia actual donde `capabilities.capture.window=false` pero `capture window` puede responder `ok=true`. Canonicalizar `backend_used` para evitar divergencia `niri-wayland` vs `niri-wayland-direct`.
  **Must NOT do**: No hardcodear capacidades en dos sitios distintos; no devolver éxito cuando capability reporta `false`.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: requiere alinear contratos entre subsistemas CLI/capabilities/capture.
  - Skills: [`zig-best-practices`] - evita duplicación de estado y divergencia de enums/strings.
  - Omitted: [`design-taste-frontend`] - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 3,4,9,10,11,12 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - Current mismatch: `src/capabilities/probe.zig:18-21` (`window:false`, `backend:"niri-wayland"`).
  - Runtime success path: `src/capture/command.zig:108-125` + `src/backends/capture_backend.zig:47-58`.
  - Backend label today: `src/backends/capture_backend.zig:129-133` (`niri-wayland-direct`).
  - E2E capabilities check: `scripts/qa/run-e2e-niri.sh:99-103`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-capabilities-consistency.sh`
  - [ ] `./zig-out/bin/shaula capabilities list --json | jq -e '.result.capture.window == .capture.window' >/dev/null`
  - [ ] `./zig-out/bin/shaula capture window --json --window-id 26 >/tmp/shaula-window.json || true; jq -e '(.ok==false and .error.code=="ERR_CAPTURE_MODE_UNSUPPORTED") or (.ok==true)' /tmp/shaula-window.json >/dev/null`
  - [ ] `zig test src/capabilities/probe.zig`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Contrato capability estricto
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-capabilities-consistency.sh`
    Expected: Exit code 0; cada modo con capability=false falla con código determinístico
    Evidence: .sisyphus/evidence/task-2-capability-contract.txt

  Scenario: Inconsistencia inducida
    Tool: Bash
    Steps: Ejecutar test con fixture que reporta `window=false` pero fuerza success
    Expected: Exit code != 0 y mensaje `ERR_CAPABILITY_EXECUTION_MISMATCH`
    Evidence: .sisyphus/evidence/task-2-capability-contract-error.txt
  ```

  **Commit**: YES | Message: `fix(capabilities): enforce strict parity with capture execution` | Files: `src/capabilities/probe.zig`, `src/capture/command.zig`, `src/backends/capture_backend.zig`, `scripts/qa/assert-capabilities-consistency.sh`

- [x] 3. Añadir QA de integridad visual (decode PNG + rechazo stub + fixture contenido)

  **What to do**: Extender QA para validar imagen real: decodificar PNG desde disco, comprobar dimensiones decodificadas vs JSON, rechazar firma stub conocida y validar non-black en fixture controlado multicolor (no en escenas reales arbitrarias). Integrar estos checks en `run-integration-tests.sh` y `run-all-tests.sh`.
  **Must NOT do**: No usar “non-black” universal sobre cualquier escena real (evitar falsos negativos en pantallas oscuras).

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: diseño de QA robusto con criterios binarios y determinísticos.
  - Skills: [] - scripts QA + parsing estándar.
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 10,11,12 | Blocked By: 1,2

  **References** (executor has NO interview context - be exhaustive):
  - QA orchestrator: `scripts/qa/run-all-tests.sh:33-35`.
  - Integration suite: `scripts/qa/run-integration-tests.sh:7-12`.
  - Existing contract-only checks: `scripts/qa/run-e2e-niri.sh:21-31`.
  - Stub bytes source for signature rejection: `src/backends/capture_backend.zig:177-195`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-capture-content-validity.sh`
  - [ ] `bash scripts/qa/run-integration-tests.sh`
  - [ ] `bash scripts/qa/run-all-tests.sh`
  - [ ] `test -f .sisyphus/evidence/task-3-capture-content-validity.json`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: PNG válido y contenido de fixture correcto
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-capture-content-validity.sh --fixture colorful-grid --mode fullscreen`
    Expected: Exit code 0; decode correcto, dimensiones coinciden, firma stub ausente, luma media > threshold fixture
    Evidence: .sisyphus/evidence/task-3-capture-content-validity.json

  Scenario: Archivo stub detectado
    Tool: Bash
    Steps: Ejecutar `python3 scripts/qa/assert_png_not_stub_signature.py tests/fixtures/images/stub-1x1.png`
    Expected: Exit code != 0 y mensaje `ERR_CAPTURE_STUB_SIGNATURE_DETECTED`
    Evidence: .sisyphus/evidence/task-3-capture-content-validity-error.txt
  ```

  **Commit**: YES | Message: `test(capture): enforce decoded image integrity and stub rejection` | Files: `scripts/qa/assert-capture-content-validity.sh`, `scripts/qa/assert_png_not_stub_signature.py`, `scripts/qa/run-integration-tests.sh`, `scripts/qa/run-all-tests.sh`, `.sisyphus/evidence/*`

- [x] 4. Alinear `daemon status` con estado IPC real

  **What to do**: Cambiar `daemon status` para consultar el servidor IPC (`daemon.status`) y devolver estado real de la state machine, en vez de inferir solo por existencia de socket. Mantener contrato JSON actual (`state`, `result.state`, `ipc_version`) pero con valor auténtico.
  **Must NOT do**: No romper `daemon start/stop`; no dejar `status=ready` fijo cuando daemon esté degradado.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: cambio de coordinación CLI↔daemon con riesgo en lifecycle.
  - Skills: [`zig-best-practices`] - útil para IO/errores en Zig.
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 10,11,12 | Blocked By: 1,2

  **References** (executor has NO interview context - be exhaustive):
  - Status actual por socket: `src/main.zig:231-251`.
  - IPC command available: `src/daemon/server.zig:98-101`.
  - State machine transitions: `src/daemon/state_machine.zig:26-49`.
  - Daemon IPC stop exchange: `src/main.zig:297-329` (patrón de request/response).

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-daemon-status-ipc-truth.sh`
  - [ ] `SHAULA_SOCKET=/tmp/shaula-status.sock ./zig-out/bin/shaula daemon start --json | jq -e '.ok==true' >/dev/null`
  - [ ] `SHAULA_SOCKET=/tmp/shaula-status.sock ./zig-out/bin/shaula daemon status --json | jq -e '.result.state|IN("ready","capturing","degraded")' >/dev/null`
  - [ ] `SHAULA_SOCKET=/tmp/shaula-status.sock ./zig-out/bin/shaula daemon stop --json | jq -e '.stopped==true' >/dev/null`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Status refleja IPC real
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-daemon-status-ipc-truth.sh`
    Expected: Exit code 0; estado reportado coincide con respuesta daemon.status
    Evidence: .sisyphus/evidence/task-4-daemon-status-truth.txt

  Scenario: Socket presente pero daemon no responde
    Tool: Bash
    Steps: Simular socket huérfano y ejecutar `shaula daemon status --json`
    Expected: Exit code != 0 y `ERR_DAEMON_NOT_RUNNING` o `ERR_IPC_TIMEOUT` determinístico
    Evidence: .sisyphus/evidence/task-4-daemon-status-truth-error.txt
  ```

  **Commit**: YES | Message: `fix(daemon): make status query real ipc state` | Files: `src/main.zig`, `src/daemon/server.zig`, `scripts/qa/assert-daemon-status-ipc-truth.sh`

- [x] 5. Cambiar output default a `~/Pictures/Shaula` con fallback explícito de error

  **What to do**: Modificar resolución de ruta por defecto para que capturas sin `--output` vayan a `~/Pictures/Shaula/capture-{mode}-{timestamp}.png`. Si `$HOME` no existe o el path no es escribible, devolver error determinístico (`ERR_OUTPUT_PATH_INVALID`) sin fallback implícito a `/tmp`.
  **Must NOT do**: No esconder fallback silencioso a `/tmp`; no romper `--output` custom path.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: afecta UX de producto y manejo de errores de filesystem.
  - Skills: [`zig-best-practices`] - gestión robusta de paths/IO en Zig.
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 6,10,11,12 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - Current default path: `src/backends/capture_backend.zig:164-167` (`/tmp/shaula/...`).
  - Output path error mapping: `src/backends/capture_backend.zig:60-71`.
  - CLI output flags: `src/capture/command.zig:153-160`, `186-193`, `219-226`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `HOME=/tmp/shaula-home ./zig-out/bin/shaula capture area --json | jq -e '.ok==true and (.path|test("^/tmp/shaula-home/Pictures/Shaula/"))' >/dev/null`
  - [ ] `bash scripts/qa/assert-default-output-path.sh`
  - [ ] `./zig-out/bin/shaula capture area --json --output /tmp/explicit-path.png | jq -e '.path=="/tmp/explicit-path.png"' >/dev/null`
  - [ ] `zig test src/backends/capture_backend.zig`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Default persistente en Pictures
    Tool: Bash
    Steps: Ejecutar `HOME=/tmp/shaula-home bash scripts/qa/assert-default-output-path.sh`
    Expected: Exit code 0; archivo generado bajo `~/Pictures/Shaula`
    Evidence: .sisyphus/evidence/task-5-default-output-path.txt

  Scenario: HOME inválido
    Tool: Bash
    Steps: Ejecutar `HOME= ./zig-out/bin/shaula capture area --json`
    Expected: Exit code != 0 y error `ERR_OUTPUT_PATH_INVALID`
    Evidence: .sisyphus/evidence/task-5-default-output-path-error.txt
  ```

  **Commit**: YES | Message: `fix(output): use Pictures/Shaula as default capture path` | Files: `src/backends/capture_backend.zig`, `scripts/qa/assert-default-output-path.sh`

- [x] 6. Migrar historial a Top-N=20 y exponerlo por `history list/show`

  **What to do**: Reemplazar modelo `latest-only` en `src/history/store.zig` por almacenamiento Top-N con recorte determinístico a 20 entradas (newest-first). Mantener compatibilidad de `history show --id latest` como alias del primer elemento. Actualizar `history list` para devolver múltiples entradas ordenadas.
  **Must NOT do**: No romper contrato JSON de campos de entrada (`path/mime/dimensions/backend_used/timestamp`); no perder determinismo de orden.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: cambio de modelo de datos con impacto en pipeline y CLI.
  - Skills: []
  - Omitted: [`design-taste-frontend`] - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 10,11,12 | Blocked By: 5

  **References** (executor has NO interview context - be exhaustive):
  - Current store latest-only: `src/history/store.zig:12-33`.
  - Current list/show behavior: `src/history/command.zig:57-127`.
  - Post-capture write to history: `src/pipeline/post_capture.zig:28-41`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-history-topn.sh --topn 20 --shots 25`
  - [ ] `./zig-out/bin/shaula history list --json | jq -e '.result.entries | length == 20' >/dev/null`
  - [ ] `./zig-out/bin/shaula history show --json --id latest | jq -e '.ok==true and .result.id=="latest"' >/dev/null`
  - [ ] `bash scripts/qa/test-history-consistency.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Recorte Top-N estable
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-history-topn.sh --topn 20 --shots 25`
    Expected: Exit code 0; lista final con 20 entradas, orden newest-first
    Evidence: .sisyphus/evidence/task-6-history-topn.txt

  Scenario: Desorden o overflow
    Tool: Bash
    Steps: Ejecutar validador sobre fixture con entradas fuera de orden/25 sin recorte
    Expected: Exit code != 0 y mensaje `ERR_HISTORY_TOPN_VIOLATION`
    Evidence: .sisyphus/evidence/task-6-history-topn-error.txt
  ```

  **Commit**: YES | Message: `feat(history): implement top-n retention and ordered listing` | Files: `src/history/store.zig`, `src/history/command.zig`, `scripts/qa/assert-history-topn.sh`, `scripts/qa/test-history-consistency.sh`

- [x] 7. Implementar overlay v1 base (gris + selección + confirmar/cancelar)

  **What to do**: Sustituir stub de `src/overlay/overlay.zig` por implementación funcional mínima: dimming gris de pantalla, selección por drag/resize, confirmación y cancelación (`Esc`). Mantener alcance base (sin presets avanzados, sin toolkit de anotaciones). Publicar resultado determinístico de selección para su consumo por `capture area`.
  **Must NOT do**: No incluir barra de herramientas avanzada tipo CleanShot en esta iteración; no acoplar persistencia/copy al loop de overlay.

  **Recommended Agent Profile**:
  - Category: `[visual-engineering]` - Reason: UX/interacción base de selección con restricciones técnicas Wayland.
  - Skills: [`uncodixfy`] - mantener UI simple/no genérica y coherente con objetivo minimal.
  - Omitted: [`frontend-design`] - no necesitamos polish alto en v1.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: 8,10,11,12 | Blocked By: 1,2,4

  **References** (executor has NO interview context - be exhaustive):
  - Current overlay stub: `src/overlay/overlay.zig:4-12`.
  - Selection data model base: `src/selection/selection.zig:3-17`.
  - Dry-run call path: `src/capture/command.zig:69-76`.
  - UX target note from user: overlay gris + selección base tipo CleanShot (sin toolkit avanzada).

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-overlay-base-selection.sh`
  - [ ] `./zig-out/bin/shaula capture area --json --dry-run | jq -e '.ok==true and .selection.cancelled==false' >/dev/null`
  - [ ] `./zig-out/bin/shaula capture area --json --dry-run --simulate-cancel | jq -e '.ok==false and .error.code=="ERR_SELECTION_CANCELLED"' >/dev/null`
  - [ ] `bash scripts/qa/benchmark-overlay-first-paint.sh --p95-max-ms 90 --p99-max-ms 130`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Selección base operativa
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-overlay-base-selection.sh`
    Expected: Exit code 0; rectángulo seleccionado y coordenadas válidas
    Evidence: .sisyphus/evidence/task-7-overlay-base.txt

  Scenario: Cancelación limpia
    Tool: Bash
    Steps: Ejecutar `./zig-out/bin/shaula capture area --json --dry-run --simulate-cancel`
    Expected: Error determinístico `ERR_SELECTION_CANCELLED` sin archivo generado
    Evidence: .sisyphus/evidence/task-7-overlay-base-error.txt
  ```

  **Commit**: YES | Message: `feat(overlay): add gray-screen base area selection workflow` | Files: `src/overlay/overlay.zig`, `src/selection/selection.zig`, `scripts/qa/assert-overlay-base-selection.sh`, `scripts/qa/benchmark-overlay-first-paint.sh`

- [x] 8. Implementar guard anti-artefactos de shell antes de capturar

  **What to do**: Introducir etapa pre-captura para evitar que UI de shell (control-center/panel animado) aparezca en screenshot: handshake “panel hidden” (si disponible) + settle barrier con timeout corto. Integrar en flujo que dispara captura desde overlay/Noctalia.
  **Must NOT do**: No depender de delays arbitrarios sin evidencia; no bloquear captura indefinidamente.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: sincronización UX/system con condiciones de carrera Wayland.
  - Skills: []
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 9,10,11,12 | Blocked By: 7

  **References** (executor has NO interview context - be exhaustive):
  - Noctalia control-center guidance (minimal trigger): `https://docs.noctalia.dev/development/plugins/control-center-widget/`
  - Plugin API actions/panel: `https://docs.noctalia.dev/development/plugins/api/`
  - External pitfall references: Niri issues on black/artifact capture (`niri-wm/niri#1872`, `#2223`, `#3700`).

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-shell-artifact-guard.sh`
  - [ ] `bash scripts/qa/assert-noctalia-capture-with-panel-hide.sh`
  - [ ] `test -f .sisyphus/evidence/task-8-shell-artifact-guard.json`
  - [ ] `bash scripts/qa/run-e2e-niri.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Captura sin artefacto de panel
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-shell-artifact-guard.sh --inject-known-marker`
    Expected: Exit code 0; marcador de panel ausente en output final
    Evidence: .sisyphus/evidence/task-8-shell-artifact-guard.json

  Scenario: Handshake no disponible
    Tool: Bash
    Steps: Simular ausencia de callback de panel y ejecutar captura
    Expected: Fallback determinístico por settle barrier; si expira, `ERR_CAPTURE_PRECONDITION_TIMEOUT`
    Evidence: .sisyphus/evidence/task-8-shell-artifact-guard-error.txt
  ```

  **Commit**: YES | Message: `fix(capture): add shell animation artifact guard before screenshot` | Files: `src/capture/*`, `src/overlay/*`, `scripts/qa/assert-shell-artifact-guard.sh`, `scripts/qa/assert-noctalia-capture-with-panel-hide.sh`

- [x] 9. Integrar plugin Noctalia MVP con menú de acciones mínimas

  **What to do**: Implementar integración plugin Noctalia opcional con botón/menú y acciones `Capture Area`, `Capture Fullscreen`, `Capture Window`, `Open Last`, `History`. Mantener control-center widget minimalista; delegar lógica principal a panel o comandos externos del CLI. Usar adapter explícito de acción→comando Shaula.
  **Must NOT do**: No mover lógica de captura al plugin; no acoplar disponibilidad de captura a instalación del plugin.

  **Recommended Agent Profile**:
  - Category: `[unspecified-high]` - Reason: integración shell externa + contratos de acción.
  - Skills: []
  - Omitted: [`frontend-design`] - diseño simple, no polish.

  **Parallelization**: Can Parallel: NO | Wave 3 | Blocks: 10,11,12 | Blocked By: 2,8

  **References** (executor has NO interview context - be exhaustive):
  - Existing PoC: `integrations/noctalia/noctalia-plugin-poc.sh`.
  - Existing optionality QA: `scripts/qa/test-noctalia-plugin-optional.sh`.
  - Existing overhead QA: `scripts/qa/benchmark-plugin-overhead.sh`.
  - Noctalia plugin docs: `https://docs.noctalia.dev/development/plugins/overview/`, `.../manifest/`, `.../api/`, `.../ipc/`, `.../panel/`.
  - Control-center widget guidance: `https://docs.noctalia.dev/development/plugins/control-center-widget/`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/assert-noctalia-actions.sh`
  - [ ] `bash scripts/qa/test-noctalia-plugin-optional.sh`
  - [ ] `bash scripts/qa/benchmark-plugin-overhead.sh --max-added-p95-ms 20`
  - [ ] `test -f .sisyphus/evidence/task-9-noctalia-actions.json`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Menú Noctalia dispara acciones válidas
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/assert-noctalia-actions.sh --with-plugin`
    Expected: Exit code 0; cada acción invoca comando Shaula esperado y devuelve JSON válido
    Evidence: .sisyphus/evidence/task-9-noctalia-actions.json

  Scenario: Plugin ausente
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/test-noctalia-plugin-optional.sh --without-plugin`
    Expected: Exit code 0; core de captura permanece funcional sin plugin
    Evidence: .sisyphus/evidence/task-9-noctalia-actions-error.txt
  ```

  **Commit**: YES | Message: `feat(noctalia): add optional mvp menu actions for shaula` | Files: `integrations/noctalia/*`, `scripts/qa/assert-noctalia-actions.sh`, `scripts/qa/test-noctalia-plugin-optional.sh`, `scripts/qa/benchmark-plugin-overhead.sh`

- [x] 10. Reforzar E2E y matriz QA consolidada post-cambios

  **What to do**: Actualizar `run-e2e-niri.sh`, `run-integration-tests.sh` y `run-all-tests.sh` para incluir validaciones nuevas (captura real, contenido, capacidades estrictas, default output, Top-N, guard anti-artefactos, Noctalia opcional). Generar reporte consolidado de evidencias y subchecks.
  **Must NOT do**: No dejar checks nuevos fuera de `run-all-tests.sh`; no depender de inspección manual de imágenes.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: orquestación QA transversal y no-regression gates.
  - Skills: []
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 3 | Blocks: 11,12 | Blocked By: 3,4,6,8,9

  **References** (executor has NO interview context - be exhaustive):
  - Current all-tests orchestrator: `scripts/qa/run-all-tests.sh`.
  - Current integration orchestrator: `scripts/qa/run-integration-tests.sh`.
  - Current e2e Niri flow: `scripts/qa/run-e2e-niri.sh`.
  - Existing evidence path pattern: `.sisyphus/evidence/task-17-test-matrix-report.json`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/run-all-tests.sh`
  - [ ] `test -f .sisyphus/evidence/task-10-postfix-test-matrix-report.json`
  - [ ] `jq -e '.pass==true and .layers.integration.pass==true and .layers.e2e_niri.pass==true' .sisyphus/evidence/task-10-postfix-test-matrix-report.json >/dev/null`
  - [ ] `bash scripts/qa/run-e2e-niri.sh`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Matriz completa post-fix en verde
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/run-all-tests.sh`
    Expected: Exit code 0; reporte JSON incluye nuevos subchecks y todos `pass=true`
    Evidence: .sisyphus/evidence/task-10-postfix-test-matrix-report.json

  Scenario: Regresión en integridad visual
    Tool: Bash
    Steps: Forzar backend de prueba inválido y ejecutar suite
    Expected: Exit code != 0; gate falla con `ERR_CAPTURE_STUB_SIGNATURE_DETECTED` o equivalente
    Evidence: .sisyphus/evidence/task-10-postfix-test-matrix-error.txt
  ```

  **Commit**: YES | Message: `test(qa): consolidate post-fix matrix for capture integrity and noctalia` | Files: `scripts/qa/run-all-tests.sh`, `scripts/qa/run-integration-tests.sh`, `scripts/qa/run-e2e-niri.sh`, `scripts/qa/assert-*.sh`, `.sisyphus/evidence/*`

- [x] 11. Actualizar contratos/especificaciones y documentación operativa

  **What to do**: Actualizar `spec/architecture.md`, `spec/testing.md`, `spec/requirements.md`, `spec/wayland-niri-integration.md` para reflejar decisiones finales: backend real sin stub runtime, capacidades estrictas, Top-N=20, default output en `~/Pictures/Shaula`, overlay v1 base, integración Noctalia MVP opcional y guard anti-artefactos.
  **Must NOT do**: No dejar contradicciones entre specs; no introducir alcance extra (OCR/scrolling/timer avanzado) en este plan.

  **Recommended Agent Profile**:
  - Category: `[writing]` - Reason: consolidación multi-doc con coherencia contractual.
  - Skills: []
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: YES | Wave 3 | Blocks: 12 | Blocked By: 2,3,6,9,10

  **References** (executor has NO interview context - be exhaustive):
  - Current architecture/status mismatch: `src/main.zig`, `src/capabilities/probe.zig`, `src/backends/capture_backend.zig`.
  - Current testing scope: `scripts/qa/run-all-tests.sh`, `scripts/qa/run-e2e-niri.sh`.
  - Existing spec baseline: `spec/*.md`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/validate-spec-cross-links.sh spec`
  - [ ] `grep -q 'Top-N 20' spec/requirements.md`
  - [ ] `grep -q '~/Pictures/Shaula' spec/requirements.md`
  - [ ] `grep -q 'capabilities strict contract' spec/architecture.md`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Specs consistentes post-fix
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/validate-all-specs.sh`
    Expected: Exit code 0; sin contradicciones ni enlaces rotos
    Evidence: .sisyphus/evidence/task-11-spec-consistency.txt

  Scenario: Contrato inconsistente
    Tool: Bash
    Steps: Validar fixture con `window=false` pero success en docs
    Expected: Exit code != 0 y mensaje `ERR_SPEC_INCONSISTENT_DECISIONS`
    Evidence: .sisyphus/evidence/task-11-spec-consistency-error.txt
  ```

  **Commit**: YES | Message: `docs(spec): align contracts with real capture and noctalia mvp` | Files: `spec/architecture.md`, `spec/testing.md`, `spec/requirements.md`, `spec/wayland-niri-integration.md`, `scripts/qa/validate-*.sh`

- [x] 12. Cierre de remediación con checklist de release interno

  **What to do**: Ejecutar checklist de release interno centrado en bugfix: validación completa de QA, revisión de error taxonomy utilizada, verificación de evidencia obligatoria y confirmación de no-regresión en comandos principales (`daemon`, `capture`, `capabilities`, `history`). Preparar resumen final ejecutable para `start-work` handoff.
  **Must NOT do**: No marcar resuelto sin evidencia de contenido de imagen; no cerrar con pendientes de decisiones críticas.

  **Recommended Agent Profile**:
  - Category: `[deep]` - Reason: consolidación de calidad final con múltiples criterios.
  - Skills: []
  - Omitted: [`frontend-design`] - no aplica.

  **Parallelization**: Can Parallel: NO | Wave 3 | Blocks: F1,F2,F3,F4 | Blocked By: 10,11

  **References** (executor has NO interview context - be exhaustive):
  - Plan actual: `.sisyphus/plans/shaula-capture-integrity-noctalia-overlay.md`.
  - QA evidence outputs: `.sisyphus/evidence/task-1-*` ... `task-11-*`.
  - Existing release-style report pattern: `.sisyphus/evidence/task-17-test-matrix-report.json`.

  **Acceptance Criteria** (agent-executable only):
  - [ ] `bash scripts/qa/run-all-tests.sh`
  - [ ] `bash scripts/qa/release-readiness-capture-fix.sh`
  - [ ] `test -f .sisyphus/evidence/task-12-release-readiness.json`
  - [ ] `jq -e '.ready==true and .blocking_issues==0' .sisyphus/evidence/task-12-release-readiness.json >/dev/null`

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Ready para ejecución posterior
    Tool: Bash
    Steps: Ejecutar `bash scripts/qa/release-readiness-capture-fix.sh`
    Expected: Exit code 0; reporte con `ready=true` y zero blockers
    Evidence: .sisyphus/evidence/task-12-release-readiness.json

  Scenario: Bloqueador crítico detectado
    Tool: Bash
    Steps: Ejecutar readiness con evidencia incompleta simulada
    Expected: Exit code != 0 y `ERR_RELEASE_BLOCKED`
    Evidence: .sisyphus/evidence/task-12-release-readiness-error.txt
  ```

  **Commit**: YES | Message: `chore(release): add capture-fix readiness checklist and report` | Files: `scripts/qa/release-readiness-capture-fix.sh`, `.sisyphus/evidence/task-12-release-readiness.json`

## Final Verification Wave (MANDATORY — after ALL implementation tasks)
> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.
> **Do NOT auto-proceed after verification. Wait for user's explicit approval before marking work complete.**
> **Never mark F1-F4 as checked before getting user's okay.** Rejection or user feedback -> fix -> re-run -> present again -> wait for okay.
- [x] F1. Plan Compliance Audit — oracle
- [x] F2. Code Quality Review — unspecified-high
- [x] F3. Real Manual QA — unspecified-high (+ playwright if UI)
- [x] F4. Scope Fidelity Check — deep

## Commit Strategy
- Commits atómicos por tarea cerrada (o subgrupo cohesivo) con evidencia QA adjunta.
- Convención: `type(scope): desc`.
- Tipos preferidos: `feat`, `fix`, `test`, `docs`, `chore`.
- No mezclar en un commit cambios de backend core y cambios de plugin Noctalia.

## Success Criteria
- Capturas ya no presentan salida stub/negra en escenarios de prueba controlada.
- QA detecta de forma determinística cualquier regresión de firma stub o contenido inválido.
- `capabilities` y ejecución son consistentes en todos los modos soportados.
- Historial devuelve Top-N=20 estable y salida default persistente en `~/Pictures/Shaula`.
- Overlay base usable y Noctalia MVP funcional sin introducir artefactos de shell en captura.
