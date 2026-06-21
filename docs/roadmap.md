# Shaula Roadmap

Shaula is a focused Wayland screenshot workflow with a native capture overlay
and post-capture preview/editor.

## Current Release Focus: v0.1.6

v0.1.6 is the **UX rough-corners release**.

The goal is not to add another large headline feature. The goal is to make the
features already present feel coherent, predictable, fast, and ready for normal
daily use.

### Capture workflow polish

- Keep Quick Capture immediate and capture-on-release.
- Keep Area Capture adjustable without making the overlay feel heavy.
- Make Quick, Area, Fullscreen, All Screens, Window, and Previous Area naming
  and behavior consistent across the CLI, Niri shortcuts, Noctalia, Settings,
  notifications, and documentation.
- Prevent overlapping hotkey invocations from producing confusing duplicate
  overlays or captures.
- Preserve the selected output, previous-area state, and logical-to-physical
  coordinate behavior across mixed-monitor layouts.
- Keep live and frozen region capture behavior understandable and reliable.

### Preview and editor polish

- Keep Copy, Save, Save As, Done, and Discard semantics clear and consistent.
- Keep temporary runtime artifacts separate from durable saved screenshots in
  notifications, folder actions, and cleanup behavior.
- Prevent toolbar, metadata, HUD, and tool changes from resizing or shifting the
  floating preview window.
- Keep primary actions usable at every supported preview size, with predictable
  overflow behavior.
- Polish keyboard behavior so shortcuts do not conflict with text editing,
  modal tools, temporary Hand/Pan, or active selections.
- Keep selection, multi-selection, annotation clipboard, duplicate, delete, and
  undo/redo behavior consistent across annotation types.
- Keep every HUD-controlled creation default persisted across sessions: colors,
  stroke widths, stroke styles, fill, corner shape, text size/font/alignment,
  opacity, Spotlight border options, and Annotation Eraser size.
- Finish rough edges in tool creation/editing transitions, especially Rectangle,
  Arrow/Line, Text, Pen, Highlight, Spotlight, and Annotation Eraser.
- Keep hit testing based on visible geometry rather than surprising bounding-box
  behavior.

### Feedback and error handling

- Make successful save/copy flows produce concise, consistent notifications.
- Keep notification actions useful without exposing internal runtime paths.
- Surface actionable errors for missing tools, unsupported runtime paths,
  invalid configuration, clipboard failures, and capture conflicts.
- Avoid silent failures from compositor keybindings, shell integrations, or
  setup-generated commands.

### Setup, integration, and first-run polish

- Keep `shaula setup` explicit about every Niri or Noctalia change before it is
  applied.
- Preserve backups and existing user configuration.
- Keep user-local installation, AUR packages, release archives, desktop files,
  icons, and helper binaries aligned.
- Ensure Settings edits only the supported public configuration contract.
- Keep Noctalia optional: capture must work normally when the widget is absent,
  disabled, or broken.

### Documentation and release quality

- Keep README, roadmap, specs, and runtime capability reporting aligned with the
  actual supported Wayland backends and compositor behavior.
- Remove stale version-specific guidance and completed implementation history
  from active handoff documentation.
- Verify release archives include every required helper, icon, desktop asset,
  and optional integration payload.
- Run the automated baseline before release:

  ```bash
  ./dev check
  git diff --check
  ```

- Manually verify the main user journeys before tagging v0.1.6:
  Quick Capture, Area Capture, Fullscreen, All Screens, Copy, Save, Save As,
  Done, Discard, Undo/Redo, Annotation Eraser, Settings, `shaula setup`, and the
  Noctalia widget.

### v0.1.6 release bar

v0.1.6 is ready when:

- the primary capture flows have no known high-impact UX regressions;
- save/copy/notification behavior is predictable;
- preview layout remains stable during normal editing;
- the documented shortcuts and integration actions work as described;
- installation and setup do not silently mutate user configuration;
- automated checks pass and the primary flows receive a manual Wayland test.

## Shipped Foundation

- Native Wayland capture overlay and GTK preview/editor.
- Quick, Area, Fullscreen, All Screens, Window, and Previous Area capture paths.
- Direct CLI orchestration with short-lived helpers and no resident daemon.
- Niri direct capture, wlroots/`grim`, and xdg-desktop-portal runtime paths.
- Noctalia Bar Widget with packaged installer integration.
- Copy, Save, Save As, Done, Discard, and actionable save notifications.
- Undo/redo history for document edits.
- Multi-annotation selection, clipboard, duplicate, move, and delete.
- Rectangle, Arrow, Line, Text, Pen, Highlight, Measure, Spotlight, Crop,
  Blur, pixel Erase, and Annotation Eraser workflows.
- Passive color sampler with Tab copy and swatch-to-active-color apply.
- Native Settings UI, setup wizard, release installer, and AUR packaging.

## After v0.1.6

Potential follow-up work should be driven by real usage rather than expanding
the toolbar by default:

- Dedicated active eyedropper mode if passive sampling and swatch apply are not
  sufficient.
- More filename templating and save-path configuration.
- Additional preview history affordances if users need more visibility or
  control.
- Explicit external text/image paste as annotations, separate from the
  preview-local annotation clipboard.
- General QuickShell integration only after the draft architecture and expected
  user value are reviewed.
- Deploy the static product landing page.

## Current Non-Goals

These are not part of the v0.1.6 UX-polish scope:

- exposing placeholders for unfinished features in the public UI;
- OCR;
- scrolling capture;
- screen recording;
- deep redaction;
- AI removal;
- smart selection;
- combining screenshots;
- Share/upload backend;
- Pin action/window persistence.
