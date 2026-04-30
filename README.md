# Shaula

Shaula es una herramienta de captura para Niri/Wayland con salida JSON determinística. El objetivo actual es simple: captura rápida, overlay de selección preciso y post-capture mínimo sobre una base Linux/Niri-first.

## Alcance actual

- Captura `all-in-one`, `area`, `fullscreen`, `window` y `previous-area`.
- Historial local de capturas.
- Integración de portapapeles para copiar o importar imágenes.
- Preview post-capture explícita con `shaula preview <file> --json`.
- Auto-preview post-capture para `capture area` y `capture all-in-one`, con
  `--preview`/`--no-preview` disponible en todos los modos de captura.
- Daemon e IPC versionados.
- Overlay de selección como línea de trabajo activa:
  - selección de área,
  - modo `all-in-one` inicial con toolbar flotante persistida,
  - confirm/cancel,
  - constraint por aspecto vía `--aspect`,
  - flujo honesto para `previous-area`,
  - helper nativo GTK/layer-shell.
- Dirección de producto:
  - captura por Niri IPC/Wayland,
  - overlay pulido,
  - una UX de selección y post-captura a la altura de Shottr,
  - mejoras incrementales primero, antes de rediseñar la UI,
  - pin screenshots,
  - pixelate/redaction,
  - ruler/color picker,
  - configuración file-first.

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
./zig-out/bin/shaula capture area --json --no-preview
./zig-out/bin/shaula capture all-in-one --json
./zig-out/bin/shaula capture fullscreen --json --preview
./zig-out/bin/shaula capture previous-area --json
./zig-out/bin/shaula preview ~/Pictures/Shaula/example.png --json
```

## Archivos clave

- `src/`: implementación Zig
- `scripts/qa/`: suites y checks automatizados
- `spec/`: contratos y decisiones de arquitectura
- `DEV.md`: guía práctica de uso y desarrollo

## Configuración

Shaula lee configuración desde `SHAULA_CONFIG_FILE`,
`$XDG_CONFIG_HOME/shaula/config.toml` o `$HOME/.config/shaula/config.toml`.
La primera superficie soportada es cómo Niri debería abrir la ventana de preview.

```bash
./zig-out/bin/shaula config show --json
./zig-out/bin/shaula config init --json
./zig-out/bin/shaula config niri-window-rule --json | jq -r '.result.kdl'
./zig-out/bin/shaula config niri-install --json
```

`config niri-install` edita sólo un bloque marcado de Shaula dentro de
`~/.config/niri/config.kdl` y crea un backup antes de modificar el archivo. La
lógica está separada del CLI para que una UI/watcher pueda reutilizarla después.
