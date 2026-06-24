# Shaula v0.1.5

Released on 2026-06-24.

v0.1.5 is a substantial Preview, capture, runtime, and documentation release.
It preserves Shaula's short-lived CLI/helper architecture while making the
normal screenshot workflow more complete and predictable.

## Highlights

### Preview editing

- Paste text or images from the system clipboard with `Ctrl+Shift+V`.
- Resize Image and committed Text annotations from corner handles.
- Use Annotation Eraser with a persistent size setting and one-step gesture
  history.
- Move single or grouped annotations from visible selection edges.
- Use improved marquee, multi-selection, duplicate, cut, copy, paste, delete,
  undo, and redo behavior.
- Persist tool HUD defaults independently from selected-object inspection.
- Keep toolbar overflow, More-menu rows, tooltips, and metadata widths stable.
- Honor the configured close-on-save behavior.

### Capture workflow

- Use `Ctrl+C` in Quick or Area capture to confirm and copy without Preview.
- Use `Ctrl+S` in Quick or Area capture to save directly without Preview.
- Preserve the configured copy behavior during direct save.
- Use the expanded Wayland runtime/backend selection and diagnostics paths.
- Keep capture execution direct and short-lived without a resident daemon.

### Settings and discovery

- Preserve custom Preview size, placement, and display settings during unrelated
  Settings saves.
- Expose the read-only `settings`, `doctor`, `preflight`, `capabilities`, and
  `explore` JSON discovery surfaces.
- Improve runtime tool lookup, helper resolution, and deterministic error
  reporting.

### Documentation and project structure

- Consolidate active engineering context and Preview contracts.
- Add maintained Wayland runtime testing guidance.
- Add the initial static product landing page.
- Record future image-composition and expandable-canvas exploration separately
  from the current release scope.

## Validation

The primary capture, Preview, Settings, and integration journeys were manually
verified on the supported Wayland/Niri environment before release.

The tagged commit must also pass the release workflow, including:

```bash
zig build -Doptimize=ReleaseSafe -Dstrip
zig build test -Doptimize=ReleaseSafe
```

The workflow verifies archive contents, checksums, and an isolated installer
smoke test before publishing the GitHub Release assets.

## Scope notes

This release does not add scrolling capture, recording, OCR, upload/share,
persistent Pin windows, smart selection, AI removal, or screenshot combining.
Those remain separate future work.
