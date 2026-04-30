Fase 0: Pulir Overlay Actual

Objetivo: que seleccionar sea rápido y confiable sin cambiar arquitectura.

Badge x y width height.
Handles claros.
Move/resize confiable.
Nudging con flechas.
Aspect ratio.
Repeat previous area.
Toolbar estable.
Confirm/cancel perfecto.
Esto ya empezamos.

Fase 1: Preview Post-Capture

Objetivo: después de capturar, no terminar el flujo inmediatamente. Estado:
implementado en primera versión, con preview explícito y auto-preview
post-capture para area/all-in-one.

shaula preview <file> --json.
Auto-preview después de capture area/all-in-one por defecto.
--preview / --no-preview en todos los modos de captura.

Ventana con imagen capturada.
Acciones básicas: copy, save, discard.
Zoom/pan.
Tamaño real / fit.
Sin herramientas de edición todavía.
El resultado de preview ya reporta action/copy/save/saved_path para preparar
canvas editable sin otra ruta paralela.

Fase 2: Canvas Editable Básico

Objetivo: acercarse al flujo Shottr de “imagen en canvas” usando la ventana
preview actual como shell.

Antes de entrar fuerte al canvas:

Configuración TOML base implementada.
shaula config show --json.
shaula config init --json.
shaula config niri-window-rule --json.
shaula config niri-install --json.
Preview app-id estable para reglas Niri.
Bloque Niri administrado con backup/idempotencia.
Base interna lista para UI/watcher.

Imagen dentro de canvas.
Selección de la imagen con handles.
Crop visual.
Padding/background simple.
Export PNG.
Copy PNG.
Toolbar superior persistente.
Aquí ya cambia bastante la UI, pero sin anotaciones complejas.

Fase 3: Redaction First

Objetivo: feature de alto valor antes que dibujitos.

Solid redaction.
Pixelate.
Blur.
Erase-like tool si aplica.
Tool options contextuales: tamaño, intensidad, modo.
Undo/redo mínimo.
Esto es más útil para Shaula que flechas/texto al principio.

Fase 4: Annotation Tools

Objetivo: herramientas clásicas.

Rectángulo.
Flecha.
Texto.
Highlight.
Free draw opcional.
Color/stroke/width.
Selección y edición de objetos.
Fase 5: Dev Tools

Objetivo: diferencial Linux/frontend.

Color picker.
Ruler.
Medición manual entre puntos.
Magnifier.
Logical vs physical pixels.
Fase 6: Pin / Floating References

Objetivo: flujo power-user.

Pin screenshot.
Drag pin.
Opacity.
Borderless.
Close hotkey/button.
