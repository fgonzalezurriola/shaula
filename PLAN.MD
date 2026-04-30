Fase 0: Pulir Overlay Actual

Objetivo: que seleccionar sea rápido y confiable sin cambiar arquitectura.

Estado: en progreso.

- Badge x/y/width/height.
- Handles claros.
- Move/resize confiable.
- Nudging con flechas.
- Aspect ratio.
- Repeat previous area.
- Toolbar estable.
- Confirm/cancel perfecto.

Fase 1: Preview Post-Capture

Objetivo: después de capturar, no terminar el flujo inmediatamente.

Estado: implementada en primera versión real.

- `shaula preview <file> --json`.
- Helper GTK con imagen capturada.
- Acciones básicas: copy, save as, discard, close.
- Zoom/pan.
- Fit / 100%.
- Resultado estructurado de preview con action/copy/save/saved_path.
- Auto-preview post-capture por defecto en `capture area` y `capture all-in-one`.
- `--preview` / `--no-preview` en todos los modos de captura.
- El JSON de captura incluye bloque `preview`.
- Fallo de preview se reporta como degradación, no como fallo de captura.
- `--dry-run` no lanza preview.

Queda fuera de Fase 1:

- Edición real sobre canvas.
- Semántica destructiva para `Discard`.

Fase 1.5: Configuración y Onboarding Niri

Objetivo: hacer simple y mantenible la configuración inicial sin que Shaula edite
configuración de Niri de forma opaca.

Estado: implementada como base inicial.

- Configuración TOML base.
- Resolución XDG de `config.toml`.
- `SHAULA_CONFIG_FILE` para override.
- `shaula config show --json`.
- `shaula config init --json`.
- `shaula config niri-window-rule --json`.
- `shaula config niri-install --json`.
- Shortcuts de desarrollo para config/Niri en `./dev`.
- Preview app-id estable: `dev.shaula.preview`.
- Preview title estable: `Shaula Preview`.
- Generación de `window-rule` Niri desde TOML.
- Instalación opcional de bloque administrado en `config.kdl`.
- Bloque administrado idempotente con markers Shaula.
- Backup antes de modificar config Niri.
- Backups únicos y no destructivos.
- Bloques administrados corruptos fallan con `ERR_CONFIG_INVALID` antes de escribir.
- Config inválida/unreadable tiene errores determinísticos `ERR_CONFIG_*`.
- Documentación inicial en `spec/configuration.md`, README, DEV y specs.

Decisión mantenible:

- Shaula define intención en TOML.
- Niri conserva la autoridad de layout mediante `window-rule`.
- Shaula no persigue ventanas vía `niri msg windows`.
- Shaula no edita automáticamente config KDL sin comando explícito.
- El overlay de selección queda fuera de esta configuración porque usa layer-shell.

Base preparada para futuro:

- UI de configuración sobre el mismo modelo TOML.
- Watcher/reload futuro sin rediseñar contratos.
- `shaula reload` como posible siguiente comando para recargar estado de Shaula, no
  para recargar Niri automáticamente sin consentimiento explícito.

Fase 2: Canvas Editable Básico

Objetivo: acercarse al flujo Shottr de “imagen en canvas” usando la ventana
preview actual como shell.

Entrada técnica lista:

- El archivo capturado sigue siendo la fuente original.
- La ventana preview actual es el shell natural del futuro canvas.
- Las acciones ya tienen resultado estructurado.
- Config Niri ya puede controlar cómo aparece la ventana preview.

Pendiente:

- Imagen dentro de canvas.
- Viewport + image model explícitos.
- Selección de la imagen con handles.
- Crop visual no destructivo.
- Padding/background simple.
- Export PNG.
- Copy PNG renderizado desde canvas.
- Toolbar superior persistente.

Fase 3: Redaction First

Objetivo: feature de alto valor antes que dibujitos.

- Solid redaction.
- Pixelate.
- Blur.
- Erase-like tool si aplica.
- Tool options contextuales: tamaño, intensidad, modo.
- Undo/redo mínimo.

Fase 4: Annotation Tools

Objetivo: herramientas clásicas.

- Rectángulo.
- Flecha.
- Texto.
- Highlight.
- Free draw opcional.
- Color/stroke/width.
- Selección y edición de objetos.

Fase 5: Dev Tools

Objetivo: diferencial Linux/frontend.

- Color picker.
- Ruler.
- Medición manual entre puntos.
- Magnifier.
- Logical vs physical pixels.

Fase 6: Pin / Floating References

Objetivo: flujo power-user.

- Pin screenshot.
- Drag pin.
- Opacity.
- Borderless.
- Close hotkey/button.
