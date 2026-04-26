# Shaula Developer Guide

Esta guía describe cómo compilar, probar y usar Shaula hoy, sin depender de artefactos de planificación externos al producto.

## Qué es Shaula hoy

Shaula es un CLI de captura para Niri/Wayland con contratos JSON estables. La meta de producto actual es una experiencia inspirada en CleanShot: captura rápida, selección limpia de área, confirmación explícita, integración con portapapeles e historial, y errores `ERR_*` determinísticos.

Superficie soportada:

- `capture area`
- `capture fullscreen`
- `capture window`
- `history list`
- `history show --id latest`
- `clipboard copy-image`
- `clipboard import-image`
- `daemon start|status|stop`
- `capabilities list`
- `errors list`

Fuera de alcance actual:

- OCR
- grabación
- scrolling capture
- placeholders visuales de features futuras

## Requisitos

- Zig `0.16.0`
- Niri
- `grim`
- `slurp`
- `wl-copy`
- `wl-paste`
- `jq`

## Build

```bash
zig build
zig build test
```

Binarios generados:

- `./zig-out/bin/shaula`
- `./zig-out/bin/shaula-overlay`
- `./zig-out/bin/shaula-overlay-feasibility-spike`

## Variables de entorno útiles

```bash
export SHAULA_COMPOSITOR=niri
export WAYLAND_DISPLAY=wayland-1
export NIRI_SOCKET=/run/user/1000/niri-0.sock
```

Variables opcionales para desarrollo y QA:

```bash
# Fuerza selección determinística no interactiva.
export SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION=1

# Helper sintético para capturas cuando no hay grim disponible.
export SHAULA_RUNTIME_CAPTURE_HELPER="$(pwd)/scripts/qa/fake_runtime_capture_helper.py"

# Binario alternativo para el helper de overlay.
export SHAULA_OVERLAY_HELPER_BIN="$(pwd)/zig-out/bin/shaula-overlay"
```

## Uso del software

### 1. Preflight

```bash
./zig-out/bin/shaula preflight --json | jq
./zig-out/bin/shaula capabilities list --json | jq
```

`preflight` valida que el entorno Wayland/Niri esté listo. `capabilities list` muestra qué modos de captura están habilitados según el runtime real.

### 2. Capturas

Captura de área interactiva:

```bash
./zig-out/bin/shaula capture area --json
```

Captura de área con aspecto fijo:

```bash
./zig-out/bin/shaula capture area --json --aspect 16:9
./zig-out/bin/shaula capture area --json --aspect 4:3
```

Captura a un archivo concreto:

```bash
./zig-out/bin/shaula capture area --json --output /tmp/shot.png
./zig-out/bin/shaula capture fullscreen --json --output /tmp/full.png
./zig-out/bin/shaula capture window --json --window-id active --output /tmp/window.png
```

Post-capture mínimo:

```bash
./zig-out/bin/shaula capture area --json --copy
./zig-out/bin/shaula capture area --json --save
./zig-out/bin/shaula capture fullscreen --json --copy --save
```

Modos de prueba:

```bash
./zig-out/bin/shaula capture area --json --dry-run
./zig-out/bin/shaula capture area --json --simulate-cancel
```

Notas del overlay:

- Si el helper gráfico real no está disponible, Shaula cae a `slurp`.
- Si no hay backend interactivo real, Shaula ya no inventa una selección exitosa implícita.
- `--dry-run` queda reservado para pruebas y QA.

### 3. Historial

```bash
./zig-out/bin/shaula history list --json | jq
./zig-out/bin/shaula history show --json --id latest | jq
```

### 4. Portapapeles

```bash
./zig-out/bin/shaula clipboard copy-image --json --input /tmp/shot.png
./zig-out/bin/shaula clipboard import-image --json --output /tmp/imported.png
```

### 5. Daemon

```bash
./zig-out/bin/shaula daemon start --json
./zig-out/bin/shaula daemon status --json
./zig-out/bin/shaula daemon stop --json
```

### 6. Taxonomía de errores

```bash
./zig-out/bin/shaula errors list --json | jq
```

## Flujo recomendado de desarrollo

```bash
zig build
zig build test
bash scripts/qa/run-integration-tests.sh
bash scripts/qa/run-e2e-niri.sh
bash scripts/qa/run-performance-gates.sh
bash scripts/qa/run-all-tests.sh
```

Perfiles de QA:

```bash
SHAULA_QA_PROFILE=fast bash scripts/qa/run-all-tests.sh
SHAULA_QA_PROFILE=full bash scripts/qa/run-all-tests.sh
SHAULA_QA_PROFILE=debug QA_KEEP_ARTIFACTS=1 bash scripts/qa/run-all-tests.sh
```

## Scripts QA relevantes

- `scripts/qa/run-all-tests.sh`: suite consolidada
- `scripts/qa/run-integration-tests.sh`: integraciones de capture, overlay, historial y Noctalia
- `scripts/qa/run-e2e-niri.sh`: recorridos end-to-end
- `scripts/qa/run-performance-gates.sh`: budgets y latencias
- `scripts/qa/release-readiness-capture-fix.sh`: checklist de release
- `scripts/qa/assert-overlay-helper-interactive.sh`: lanes del helper interactivo

## Troubleshooting

`ERR_UNSUPPORTED_COMPOSITOR`

```bash
echo "$SHAULA_COMPOSITOR"
echo "$WAYLAND_DISPLAY"
echo "$NIRI_SOCKET"
```

Shaula v1 es Niri-first. Si el compositor no coincide, el preflight y capture van a fallar de forma determinística.

`ERR_CAPTURE_BACKEND_UNAVAILABLE`

```bash
command -v grim
```

Si `grim` no está disponible, configurá `SHAULA_RUNTIME_CAPTURE_HELPER` para QA o instalá el binario real.

`ERR_OVERLAY_UNAVAILABLE`

```bash
command -v slurp
test -x ./zig-out/bin/shaula-overlay && echo helper-ok
```

El helper de overlay requiere dependencias UI reales para funcionar como proceso interactivo. Si no están presentes, Shaula usa `slurp` como fallback.

`ERR_SELECTION_CANCELLED`

El usuario canceló la selección o el flujo interactivo terminó sin una geometría válida.

## Estructura breve del repo

```text
src/
  main.zig
  capture/
  overlay/
  backends/
  daemon/
  history/
  clipboard/

spec/
  architecture.md
  requirements.md
  wayland-niri-integration.md

scripts/qa/
  run-all-tests.sh
  run-integration-tests.sh
  run-e2e-niri.sh
  run-performance-gates.sh
```
