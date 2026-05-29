# Plan Wayland

## Objetivo

Agregar compatibilidad Wayland generica mediante **xdg-desktop-portal Screenshot** como backend de compatibilidad, manteniendo `grim`/Niri/wlroots como fast path cuando entreguen mejor UX. Window capture permanece deshabilitado a nivel producto por ahora, aunque la deteccion de capacidades debe registrar si el portal lo expone.

## Restricciones

- Cambios centrados en codigo (no depender de markdowns como fuente de verdad).
- Sin nueva suite de regresion.
- Mantener errores `ERR_*` deterministas.
- Emitir warnings explicitos cuando se use backend degradado o seleccion portal.
- Mantener overlay solo en compositores con soporte real (wlroots layer-shell).
- Actualizar `CONTEXT.md` tras cada fase relevante.

---

## Fase 1: Backend portal (compatibilidad base)

Archivos a tocar:

| Archivo | Que hacer |
|---|---|
| `src/backends/portal_screenshot.zig` (nuevo) | Implementar cliente `xdg-desktop-portal` Screenshot via `gdbus` o helper wrapper. Obtener imagen del portal y copiarla a `output_path`. |
| `src/backends/capture_execution_plan.zig` | Resolver plan para `portal-screenshot`: invocar el helper por encima de `grim`. |
| `src/backends/capture_backend_runtime_exec.zig` | Ejecutar plan portal y mapear errores a `error.BackendUnavailable` u otros codigos existentes. |
| `src/backends/capture_backend.zig` | Reportar backend degradado cuando sea `portal-screenshot` (ya tiene logica parcial para `degraded_backend`). |
| `src/capabilities/runtime.zig` | Ajustar labels, fallbacks y matriz de modos para `portal-screenshot`. |

Detalles:

- El helper portal debe aceptar `--backend portal-screenshot --mode <mode> --output <path>` igual que el fake helper de QA.
- Modos soportados por portal: `area`, `fullscreen`, `all_screens`. `window` se queda en `false` en producto pero se registra si el portal lo expone.
- Errores mapeables: portal no disponible (`ERR_CAPTURE_BACKEND_UNAVAILABLE`), timeout (`ERR_IPC_TIMEOUT`), fallo desconocido (`ERR_UNKNOWN_UNMAPPED`).

---

## Fase 2: Capacidades y deteccion

Archivos a tocar:

| Archivo | Que hacer |
|---|---|
| `src/compositor/runtime.zig` | Marcar `.wayland` como soportado si hay portal disponible. Agregar deteccion de portal (ej. `SHAULA_PORTAL_AVAILABLE` o chequeo de binario `gdbus`). |
| `src/preflight/probe.zig` | No devolver `ERR_UNSUPPORTED_COMPOSITOR` en Wayland generico si portal esta disponible. Emitir warning `portal_fallback`. |
| `src/capabilities/probe.zig` | Listar capacidades completas aun en Wayland generico. Incluir flag `portal_window_capable` en JSON si el portal reporta soporte de ventana. |
| `src/capabilities/runtime.zig` | Reflejar portal en la matriz de modos. Prioridad de backend: (1) stub, (2) Niri directo, (3) portal forzado, (4) default -> portal si Wayland generico, grim si wlroots/Niri. |
| `src/doctor/diagnostics.zig` | Agregar `portal_available` al reporte. Emitir warning si no hay portal y el compositor no es Niri. |

Detalles:

- Deteccion de portal: probar `gdbus call --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.DBus.Properties.Get org.freedesktop.portal.Screenshot version` o similar.
- Si no hay portal ni Niri/wlroots, mantener `ERR_UNSUPPORTED_COMPOSITOR`.

---

## Fase 3: Seleccion y overlay

Archivos a tocar:

| Archivo | Que hacer |
|---|---|
| `src/capture/lifecycle.zig` | Si overlay no esta disponible (compositor sin layer-shell), usar seleccion interactiva via portal en vez de overlay. |
| `src/overlay/selection_session.zig` | Propagar `unavailable` limpiamente cuando helper overlay falla con `ERR_OVERLAY_UNAVAILABLE`. El caller en lifecycle decide fallback a portal. |
| `src/runtime/previous_area_store.zig` | Deshabilitar `previous-area` bajo portal (no tenemos geometria confiable del portal). |
| `src/pipeline/post_capture.zig` | Emitir warning `capture_selection_portal` cuando la seleccion vino del portal. |
| `src/capture/command_json.zig` | Agregar warning token `capture_selection_portal` a la lista conocida. |

Detalles:

- La seleccion interactiva del portal se invoca con `org.freedesktop.portal.Screenshot.Screenshot` con opcion `interactive=true`.
- No hay geometria devuelta por el portal, asi que `previous-area` no funciona. El modo `previous-area` debe devolver `ERR_CAPTURE_MODE_UNSUPPORTED` bajo portal.
- El overlay (gtk4-layer-shell) se mantiene solo en compositores wlroots/Niri.

---

## Fase 4: Fast path wlroots/Niri

Archivos a tocar:

| Archivo | Que hacer |
|---|---|
| `src/capabilities/runtime.zig` | Mantener `grim` solo para backends wlroots/Niri. Detectar wlroots via `XDG_CURRENT_DESKTOP` (sway, hyprland, river, wayfire, etc.) o por presencia de `wlr-screencopy` implícita. |
| `src/compositor/runtime.zig` | Agregar tokens wlroots conocidos a `isWaylandToken` o一个新的 metodo `isWlroots`. |
| `src/compositor/focused_output.zig` | Resolver `focused_output` solo para Niri (via `niri msg`) y posiblemente sway (via `swaymsg`). Para otros wlroots, devolver `null`. |
| `src/backends/capture_execution_plan.zig` | Usar grim solo con backends wlroots. Para Niri, mantener ruta directa. |

Detalles:

- Si el compositor es wlroots (sway, hyprland, river, wayfire, etc.) y `grim` esta disponible, usar grim como fast path.
- Si es Niri, usar `niri_wayland_direct` como hoy.
- Si es Wayland generico (GNOME, KDE, etc.), usar portal.

---

## Fase 5: Warnings, diagnosticos y build

Archivos a tocar:

| Archivo | Que hacer |
|---|---|
| `src/pipeline/post_capture.zig` | Emitir `capture_backend_degraded` cuando se use portal. Ya existe logica parcial para `degraded`. |
| `src/doctor/diagnostics.zig` | Extender con deteccion de portal, backend activo, y warning si no hay backend viable. |
| `build.zig` | Si se crea helper binario para portal, agregar target de build e instalacion. |
| `scripts/install.sh` | Agregar `xdg-desktop-portal` y `xdg-desktop-portal-gtk` (o el backend del DE) como dependencias recomendadas. |

---

## Verificacion

```bash
./dev check
git diff --check
```

Para cambios en overlay/selection UX:

```bash
./dev capture
./dev all
```

Confirmar manualmente:

- Niri: overlay funciona, grim usado, sin warnings de degradacion.
- GNOME/KDE (con portal): captura via portal, warning `capture_backend_degraded` presente, overlay no se muestra.
- Sin portal ni Niri: `ERR_UNSUPPORTED_COMPOSITOR`.
- Window capture: deshabilitado en todos los casos (producto), pero `capabilities list` muestra `window_capable` si el portal lo expone.
