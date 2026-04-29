# Shaula

Shaula es una herramienta de captura para Niri/Wayland con salida JSON determinística. El objetivo actual es simple: ofrecer una base tipo CleanShot, centrada en capturas rápidas, overlay de selección y post-capture mínimo, sin arrastrar roadmap especulativo.

## Alcance actual

- Captura `all-in-one`, `area`, `fullscreen`, `window` y `previous-area`.
- Historial local de capturas.
- Integración de portapapeles para copiar o importar imágenes.
- Daemon e IPC versionados.
- Overlay tipo CleanShot como línea de trabajo activa:
  - selección de área,
  - modo `all-in-one` inicial con toolbar flotante persistida,
  - confirm/cancel,
  - constraint por aspecto vía `--aspect`,
  - flujo honesto para `previous-area`,
  - helper nativo GTK/layer-shell,
  - strategy benchmark para comparar `gtk4-layer-shell|raylib|raylib-clay`.

Fuera de alcance por ahora:

- OCR
- grabación de pantalla
- scrolling capture
- placeholders de features futuras en la UI pública

## Requisitos

- Zig 0.16.0
- Niri
- `grim`
- `wl-copy` y `wl-paste`
- `jq` para QA y depuración

## Arranque rápido

```bash
zig build

export SHAULA_COMPOSITOR=niri
export NIRI_SOCKET=/run/user/1000/niri-0.sock

./zig-out/bin/shaula preflight --json
./zig-out/bin/shaula capture area --json
./zig-out/bin/shaula capture all-in-one --json
./zig-out/bin/shaula capture previous-area --json
```

## Archivos clave

- `src/`: implementación Zig
- `scripts/qa/`: suites y checks automatizados
- `spec/`: contratos y decisiones de arquitectura
- `DEV.md`: guía práctica de uso y desarrollo
