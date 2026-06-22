# ADR 0001: Preview paste architecture

## Status

Accepted.

## Context

The Preview has a window-local annotation clipboard. GTK reads the desktop clipboard asynchronously, and image payloads must remain editable after the clipboard provider disappears.

## Decision

`Ctrl+V` remains internal annotation paste. `Ctrl+Shift+V` uses a dedicated asynchronous controller with a cancellable operation and a weak window reference. Async callbacks recover Preview state only while the window is alive, and clipboard changes cancel the pending operation.

Clipboard images use a dedicated Image annotation. Each annotation owns its decoded `GdkPixbuf`. History snapshots, duplicate, and internal copy/paste deep-copy the pixel payload. Crop intersects both the displayed rectangle and source pixels. Image is preferred over text, so one invocation inserts at most one annotation.

Pure helpers calculate initial placement and validate payload limits. Images preserve aspect ratio, never upscale, center in the visible image intersection, and are limited to 16,384 px per side and 32 megapixels. Text uses current Text defaults and is clamped inside the base image.

## Consequences

Image history entries consume memory proportional to their pixel payload. In return, rendering, export, undo, duplicate, crop, and internal clipboard operations never depend on the system clipboard after paste completes.
