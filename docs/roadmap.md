# Shaula Roadmap

Shaula is a focused, Niri-friendly screenshot workflow with a native overlay
and post-capture preview/editing.

## Shipped

- Noctalia Bar Widget (QML, packaged, installer, CI-verified)
- ruler / Measure tool (`8`, edge detection, HUD)
- redaction tools (pixelate blur + smart erase, contextual region actions)
- preview Copy, Save, Save As, and Done/accept flows
- preview undo/redo history for document edits
- passive color sampler with Tab copy and swatch-to-active-color apply
- default save-folder handling with Shaula-specific filenames

## v0.1.1 Polish

- Keep preview history bounded and predictable for undo and redo.
- Keep default filenames human-recognizable:
  `YYYYMMDD-HHMMSS.png` from preview save flows and direct no-preview saved
  captures, with `-2`, `-3`, and so on for collisions.
- Keep README/specs honest about Niri-only support, logical-vs-physical pixel
  handling, hidden Pin/Share status, and screenshot-only scope.
- Manual verification target: Pin absent, Copy, Save, Save As, and Done/accept
  flows still produce the expected helper result JSON and notifications.

## Planned Features

- Dedicated active eyedropper mode if the swatch apply shortcut is not enough.
- More filename templating/configuration.
- More preview history affordances if needed by real use.

## Current Non-Goals

- automatically editing Niri config from the installer
- exposing placeholders for future features in the public UI
- OCR
- scrolling capture
- screen recording
- deep redaction
- AI removal
- smart selection
- combine screenshots
- Share backend
- Pin action/window persistence
