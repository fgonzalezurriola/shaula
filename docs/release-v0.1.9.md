# Shaula v0.1.9

v0.1.9 adds configurable capture shortcuts, bounded image composition, and
central save naming and destination settings.

## Included

- Settings edits four unique preferred capture shortcuts and applies them to
  the live portal or managed Niri backend.
- The composition sheet combines up to four files, clipboard images, or recent
  captures vertically or horizontally with gap, alignment, and sizing controls.
- Preview can expand its opaque canvas to the right and down, fit the document
  to the window, and undo or redo composition and annotation edits.
- Save configuration supports a default directory and filename templates using
  `{date}`, `{time}`, and `{datetime}`. Quick save, Save As, capture, and folder
  actions share the same destination logic.
- Preview Copy always publishes the current composited image, including selected
  annotations. `shaula run` is accepted as an alias for `shaula launch`.
- Release CI builds and verifies x86_64 and AArch64 archives before publishing
  GitHub assets and the `shaula` and `shaula-bin` AUR packages.

## Validation

- The normal Meson build and all 36 tests pass.
- Installer, release, archive, and AUR metadata contracts pass on x86_64.
- `git diff --check` passes.
- The latest `master` CI run before release passed GCC and Clang normal and
  sanitizer jobs.
- The release workflow must complete its QEMU-backed AArch64 build and archive
  validation before it can publish artifacts or AUR metadata.

## Known validation gap

GNOME and KDE portal capture and GlobalShortcuts journeys were not exercised in
real desktop sessions for this release. v0.1.9 does not claim those journeys as
validated. Niri remains the manually exercised graphical environment.
