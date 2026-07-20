# Shaula Roadmap

Shaula captures, annotates, saves, and copies screenshots on Wayland with a
native capture overlay and post-capture Preview/editor.

## Released: v0.1.5

v0.1.5 was released on 2026-06-24 after manual validation of the primary
Wayland/Niri journeys. Detailed historical notes live in
`docs/release-v0.1.5.md`.

## Release candidate: v0.1.6

v0.1.6 completes the Meson/C production port and the universal Wayland release
contracts described by ADR-0002 and ADR-0003.

The release bar includes:

- deterministic native `grim` versus Screenshot portal selection;
- a bundled clipboard provider with bounded readiness, complete child cleanup,
  detached successful lifetime, and race-free provider replacement;
- manifest-backed x86_64 and AArch64 release archives;
- installer architecture selection and complete payload validation;
- source and binary AUR metadata prepared for both architectures;
- exact tag/version and release-note gates in release CI;
- no known failing unit, contract, strict, sanitizer, install, archive, or package
  metadata check;
- live Niri capture validation where the current environment permits it.

GNOME and KDE interactive portal/graphical validation is not checked for v0.1.6
and is explicitly deferred to v0.1.7. The v0.1.6 release must not claim that
those graphical checks passed.

Detailed notes and release procedure:

- `docs/release-v0.1.6.md`
- `docs/releasing.md`
- `docs/adr/0003-universal-wayland-runtime.md`
- `docs/adr/0004-global-shortcuts-provider.md`

## Shipped foundation

- Native Wayland capture overlay and GTK Preview/editor.
- Quick, Area, Fullscreen, All Screens, Window, and Previous Area capture paths.
- Direct CLI orchestration with short-lived capture helpers and one optional,
  narrowly scoped resident provider for approved portal shortcuts.
- Niri/wlroots `grim` capture and generic Screenshot portal capture routes.
- Noctalia Bar Widget with packaged installer integration.
- Copy, Save, Save As, Done, Discard, and actionable save notifications.
- Undo/redo history for document edits.
- Multi-annotation selection, clipboard, duplicate, move, and delete.
- Explicit system-clipboard paste for text and image annotations.
- Rectangle, Arrow, Line, Text, Pen, Highlight, Measure, Spotlight, Crop,
  Blur, pixel Erase, and Annotation Eraser workflows.
- Passive color sampler with Tab copy and swatch-to-active-color apply.
- Native Settings UI, setup wizard, release installer, and AUR packaging.

## After v0.1.6

The image-composition and expandable-canvas work remains planned rather than
part of the v0.1.6 release. Its design material remains in:

- `docs/plan-image-composition.md`
- `docs/preview-expandable-canvas-design.md`
- `docs/plan-v0.1.6.md` as the original, superseded release-number proposal.

## v0.1.7

Consolidated pending work, driven by real usage:

Release-hardening completed in the current checkout:

- Settings exposes portal-owned configuration for all four capture shortcuts.
- The universal menu remains available when GlobalShortcuts is unsupported.
- `./dev install` is the complete local development installation command.
- Portable and AUR installation paths detect required runtime infrastructure;
  important failures are prominent and actionable.
- Active development and release documentation uses the current install flow.

Release validation completed for the current checkout:

- Strict Werror and ASan/UBSan builds pass all 36 tests.
- Installer and release contracts pass.
- `./dev install` completes with capture-route, Preview, clipboard, Niri rule,
  and Noctalia validation on the current CachyOS/Niri session.
- Both AUR `.SRCINFO` files match locally regenerated `makepkg` metadata and
  both package hooks pass Bash syntax validation.

Release validation still required:

- Repeat the automated gates after the version bump and final release commit.
- Build and validate optimized x86_64 and native/QEMU AArch64 archives.
- Complete GNOME and KDE graphical portal validation, including capture,
  shortcut approval, shortcut configuration, activation, and persistence.

Product work not required for the portal/install release slice:

- Update the GitHub repo description after release to match the canonical
  project phrase: `Capture, annotate, save, and copy screenshots on Wayland`.
- Re-record the demo clip to reflect v0.1.6 (clipboard via `wl-copy`, current
  Preview UI) before using it in the README/landing page.
- Finish and deploy the static product landing page (consistent hero phrase,
  current clip).
- Bounded image composition and canvas expansion.
- Dedicated active eyedropper mode if passive sampling is insufficient.
- More filename templating and save-path configuration.
- Additional Preview history affordances.
- Broader QuickShell integration after architecture and user-value review.

## Current non-goals

- exposing placeholders for unfinished features;
- infinite canvas;
- OCR;
- scrolling capture;
- screen recording;
- deep or semantic redaction;
- AI removal or automatic background removal;
- smart selection;
- global clipboard history;
- multi-page documents;
- Share/upload backend or collaboration;
- Pin action/window persistence;
- perspective transforms or freeform image distortion;
- turning Preview into a general-purpose design application.
