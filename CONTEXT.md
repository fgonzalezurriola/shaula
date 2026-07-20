# Shaula Context

Last updated: 2026-07-20

## Current release

The current release is **v0.1.8**. The production application,
helpers, tests, packaging, and release workflows are implemented in C and built
with Meson. The earlier v0.1.6 image-composition proposal is retained only as
historical design input in `docs/plan-v0.1.6.md`; image composition and
expandable-canvas work are not part of this release.

The release notes are in `docs/release-v0.1.8.md`. The release procedure and AUR
finalization steps are in `docs/releasing.md`.

## Runtime architecture

Shaula's capture, Preview, Settings, and CLI commands remain short-lived. ADR-0004
introduces one narrowly scoped optional resident process,
`shaula-shortcut-provider`, only to own an approved GlobalShortcuts portal
session and dispatch activations to the existing capture commands.

Capture routing follows ADR-0003:

- Niri and compatible wlroots sessions use the native `grim` route.
- Other supported Wayland sessions use the Screenshot portal route.
- Missing or unavailable routes return deterministic errors.
- The release and installer payload includes the CLI, overlay, Preview,
  Settings, shortcut provider, portal screenshot helper, crop helper, clipboard
  provider, desktop entry, icons, Noctalia integration, and release manifest.

The `./dev` live commands export build-tree helper paths explicitly, so they
exercise the current checkout rather than relying on a previous local install.

The generic shortcut module is shared by Settings, `shaula setup`, and explicit
`shaula shortcuts` commands. It prefers a verified XDG GlobalShortcuts portal
session and preserves desktop approval, remapping, and rejection. If the portal
is not viable and a Niri config exists, Shaula checks for conflicts, creates a
full backup, and installs only a marked managed keybinding block. Unsupported
shortcut setup remains non-fatal because the universal menu and desktop launcher
actions invoke capture commands directly.

Shortcut choice state remains separate from installed shortcut state, but it no
longer blocks application launch. `shaula launch` always opens the compact GTK
launcher with the four capture modes, Settings, the screenshots folder, and
problem reporting. Settings presents global shortcuts as an optional first
section and keeps the launcher available as the universal fallback. Shortcut
labels appear only while the shortcut backend reports them active.
The terminal aliases `shaula` and `shaula menu` route to the same stable
`shaula launch` command used by the desktop entry.
The portal provider uses a user-owned XDG autostart entry created only after
explicit enablement and removed on disable or uninstall.

The universal launcher disables GTK animations for its process and unmaps its
window before dispatching an action. Capture dispatch waits 150 ms, exceeding
the current Niri 100 ms close animation, so a composited closing menu cannot be
included in capture output.

## Clipboard publication and replacement

Clipboard publication uses `wl-copy` through the central process-execution
module. `wl-copy` owns the Wayland selection through data-control, avoiding the
input-event serial that prevents a standalone GTK/GDK CLI helper from reliably
claiming the clipboard on Niri and other wlroots compositors.

Deterministic clipboard outcomes are:

- provider exit code 35: clipboard unavailable;
- provider spawn failure: clipboard unavailable;
- malformed readiness: protocol invalid;
- readiness timeout: timeout;
- other provider exit: provider failed;
- text-copy failures: text-specific user-facing messages;
- readiness protocol stays off caller stdout and diagnostics remain on stderr.

Provider replacement implements ADR-0003 with a private session-bus
prepare/commit protocol and a unique clipboard MIME marker. For v0.1.6
providers, provider A remains alive until provider B:

1. owns the Wayland clipboard;
2. owns `dev.shaula.ClipboardProvider`;
3. emits readiness.

Only then does B commit A's exit. Failure or timeout by B lets A retain or
restore its valid selection. Clipboard replacement without the prepared marker
is external replacement and terminates the active provider.

A provider from before v0.1.6 has no handoff object. That one-time upgrade case
is detected explicitly and replaced through the legacy clipboard/name-loss
behavior, preventing an older provider from making all future clipboard copies
unavailable. All v0.1.6-to-v0.1.6 replacements use the ordered protocol.

Focused lifecycle and multi-process tests cover unavailable exit, spawn failure,
malformed readiness, timeout cleanup, no late clipboard claim, generic provider
failure, detached successful lifetime, stdout isolation, text error wording,
ordered replacement, failed replacement, and legacy-provider upgrade.

## Release artifacts and installer

Release CI builds, tests, stages, validates, checksums, and publishes:

- `shaula-linux-x86_64.tar.gz`
- `shaula-linux-aarch64.tar.gz`

AArch64 uses a QEMU-backed native `linux/arm64` Ubuntu build. The archive
validator checks every installed executable with `readelf`, verifies executable
modes, requires the exact manifest-backed payload, rejects symlinks and unlisted
files, and confirms that `share/icons/hicolor/index.theme` is absent.

The installer maps `x86_64` and `amd64` to the x86_64 archive, and `aarch64` and
`arm64` to the AArch64 archive. Installer contract tests exercise all four
machine strings, checksum verification, payload installation, and the missing
payload failure path.

Release CI rejects a tag unless it is exactly `v` plus the Meson project version
and `docs/release-<tag>.md` exists with the matching heading.

## AUR state

Tag release CI publishes both `aur/shaula` and `aur/shaula-bin` to the AUR as
`0.1.8-1`, with final immutable checksums and regenerated `.SRCINFO` files in
the temporary AUR clones.

- `shaula` builds the Meson/C source tree from tag `v0.1.8` on x86_64 and
  AArch64.
- `shaula-bin` selects the matching x86_64 or AArch64 release archive and
  installs the complete manifest-backed payload.
- `wl-clipboard` is a required runtime dependency for both packages.
- AUR publication uses the dedicated unencrypted deploy key
  `~/.ssh/id_aur` (comment `shaula-aur-deploy`), registered in the AUR
  account. It is independent from the personal `id_ed25519` key.

Manual AUR publish is replaced by the release pipeline (see
`docs/releasing.md`). Checked-in PKGBUILDs intentionally retain `SKIP`; the
workflow replaces those markers only in writable AUR clones before pushing.

## Developer workflow

- The canonical short project description is
  `Capture, annotate, save, and copy screenshots on Wayland`. It is used for
  desktop metadata, package descriptions, the README opening, and the product
  landing page; the GitHub repository description still needs the same manual
  update after release.
- The Astro landing page now uses a flat, editorial visual system without the
  previous glow, grid, or gradient treatment. Its v0.1.8 content describes the
  universal launcher, automatic grim/portal routing, optional shortcut setup,
  current annotation workflow, and both portable and AUR installation paths.
  The hero install card uses Linux and AUR tabs. AUR shows both package install
  and `shaula setup` as explicit command lines; each tab has one icon-only copy
  control inside the command panel, with no source/footer row or follow-up note.
- The landing page now reserves its compact product descriptor for the hero,
  removes non-semantic feature numbering, and uses a full-width post-install
  row instead of a colored side stripe. Orange remains concentrated in product
  status and primary actions so the visual identity survives the quieter pass.
- Landing-page text color now uses explicit ink, muted, and quiet roles instead
  of white-alpha utilities. The quiet metadata token is brand-tinted and clears
  WCAG AA at 5.33:1 against the base surface; orange is reserved for primary
  actions and meaningful automatic/stable states.
- The landing-page polish pass adds balanced heading and prose wrapping,
  complete pressed/reduced-motion states, and 44px copy controls. Copy success
  is announced through a live region; when clipboard access fails, the command
  is selected for manual copying. The unused duplicate product-media component
  was removed to prevent content drift.
- The landing-page demo is delivered as a muted, autoplaying, looping HTML video
  instead of an animated GIF. The primary VP9 WebM and H.264 MP4 fallback are
  1280x720 at 30 fps, with a lightweight WebP poster and no audio. The current
  edit keeps the recording from 11 seconds through one second before its end.
- The landing page is now intentionally limited to four beats: a plain Wayland
  screenshot-tool hero, portable/AUR install commands, the product demo, and a
  GitHub/star call to action. The open-source section includes one concise stack
  paragraph covering C11, GTK4, GLib/GIO, Cairo/Pango, gtk4-layer-shell, and
  Meson. Its GitHub action is vertically centered against the complete text
  block and includes the GitHub mark for faster recognition. The feature inventory, capture-routing table, implementation stats,
  trust-label list, release-note CTA, and repeated marketing explanations were
  removed as redundant complexity.
- The install selector now sits in the hero's right column on desktop and stacks
  below the headline on narrow screens. Its Linux/AUR switch is a compact
  segmented control; commands wrap without horizontal scrolling, and the copy
  action is a small corner control rather than a full-height column. The desktop
  column is widened enough for the portable command to settle into two lines.
  The mobile header hides secondary anchor links before they crowd the brand, while the
  GitHub action keeps a visible star icon and an accessible label at every width.
- Landing-page typography now mirrors Shaula's two annotation text modes: Geist
  is the primary interface and command face, while Excalifont is reserved for
  selective handwritten accents. Both webfont sources are version-pinned and
  retain local-font fallbacks for installed development environments.
- The navbar and browser favicon both import the canonical application mark from
  `src/preview/icons/hicolor/scalable/apps/shaula.svg`; the landing page no longer
  substitutes a lettermark or simplified favicon for the Shaula brand asset.
  The canonical SVG is transparent and tightly cropped so desktop shells and
  web surfaces provide the surrounding background instead of rendering a nested
  dark tile. Astro emits the imported favicon with a content hash, preventing old
  cached lettermark assets from surviving icon changes. Meson uses
  `rsvg-convert` to generate the installed 48, 64, 128, 256, and 512 pixel
  desktop assets from the same source. The browser title remains `Shaula`.
- `./dev` now imports the active graphical session without forcing Niri or a
  hard-coded Wayland display, so runtime compositor detection is exercised.
- Build-tree runs resolve the shortcut provider and graphical helpers from the
  current checkout instead of accidentally mixing in an older local install.
- `./dev menu` opens the universal launcher and `./dev shortcuts` reports the
  current portal status; an explicit shortcut action can be appended.
- `./dev doctor` reports Niri state as optional because the universal Wayland
  workflow does not require Niri.
- `./dev install` is the canonical complete local development install. It builds
  the checkout, installs all application payload and integrations, accepts the
  installer prompt automatically, and reloads Noctalia when present.
- `./dev uninstall` removes the files and optional integrations installed by
  `./dev install` or `./dev dev-install`, while preserving the Shaula config file.
- Settings exposes **Configure Shortcuts** whenever the portal provider is
  running. The desktop portal remains responsible for selecting and approving
  the bindings for Quick, Area, Fullscreen, and All Screens.
- Shortcut actions hide their `GtkFlowBoxChild` containers as a unit, preventing
  empty cells. Unsupported state exposes **Try Again** and suppresses the
  redundant status-only **Check Again** action.
- Both public installation routes detect required runtime support. AUR delegates
  declared dependency installation to pacman and its hooks emit red warnings for
  missing capture infrastructure; the portable installer checks requirements
  before writing user files and renders fatal diagnostics in red on a terminal.

## Verified release state

The v0.1.8 setup UX patch and the v0.1.7 shortcut, Settings, installer, and
documentation changes have now
passed strict Werror and ASan/UBSan builds with all 36 tests in each build. The
installer and release contracts pass, `./dev install` completes on the current
CachyOS/Niri session, and locally regenerated `makepkg --printsrcinfo` output
matches both checked-in AUR `.SRCINFO` files. A container repetition of the AUR
metadata check was unavailable because the installed Docker client has no active
daemon; local makepkg validation covered the same generated metadata comparison.

The current source state has passed:

- normal x86_64 build and all 36 Meson tests;
- focused shortcut backend, provider lifecycle, launch routing, Settings mapping, and
  desktop-entry action tests;
- strict x86_64 Werror build and all 36 Meson tests;
- x86_64 ASan/UBSan build and all 36 Meson tests;
- the release, installer, manifest, desktop-entry, and AUR metadata contract
  tests;
- optimized x86_64 archive validation remains required before release;
- QEMU-backed native AArch64 Werror build and archive validation remain required
  for the updated shortcut-provider payload before release;
- local development install through `./dev install`;
- `git diff --check`;
- focused clipboard lifecycle and replacement tests;
- installer architecture-selection and AUR metadata consistency checks.
- CI regenerates AUR `.SRCINFO` files from writable temporary package copies;
  the repository remains mounted read-only inside the Arch validation container.
- Build and release containers install the required `wl-clipboard` runtime, and
  both workflows use the same writable-copy AUR metadata validation.
- GitHub workflows use Node.js 24-based v5 checkout and artifact actions.
- GitHub Clang ASan/UBSan builds require explicit conversion of
  `g_io_error_from_errno()` results before passing them to `g_set_error()`;
  `CC=clang ./dev sanitize-check` covers this warning-as-error path locally.
- Release AUR publication requires the `AUR_SSH_KEY` repository secret and now
  fails before SSH setup with an explicit GitHub Actions error when it is absent.
- The release workflow hardens publication: a tag-scoped `concurrency` group
  prevents two pipelines from publishing the same tag in parallel, every job
  has bounded `timeout-minutes`, and `publish-release` runs in the `release`
  GitHub environment so repository admins can attach required reviewers and
  environment-scoped secrets. The optional `AUR_AUR_FINGERPRINT` secret pins
  the `aur.archlinux.org` Ed25519 hostkey so a silent hostkey rotation or
  MitM fails closed at AUR push time.

Live Niri validation:

- A real portal enable attempt returned `unsupported`; `./dev install` then
  selected the Niri fallback and reported `enabled: capture shortcuts (niri)`.
- Before editing, Shaula created
  `config.kdl.shaula-backup-1784515129`; its SHA-256 exactly matches the
  pre-install config hash. The resulting config contains one balanced managed
  keybinding block and preserves all user-owned content.
- Public shortcut status reports backend `niri`, state `active`, activation
  ready, and the four `Ctrl+Shift+1` through `4` triggers. No portal-provider
  autostart entry remains.
- `niri validate` accepted the updated config, and `niri msg action
  load-config-file` reloaded it successfully.
- At 20:01:21, more than 15 minutes after that capture completed, the NVIDIA
  driver began reporting Xids 56 and 11 against Niri. The sequence progressed
  through a GSP timeout to Xid 79 (`GPU has fallen off the bus`) at 20:03:23.
  The NVIDIA-owned HDMI output froze while the AMD-owned laptop panel and Niri
  IPC remained responsive; `nvidia-smi` could no longer obtain a device handle.
  Two later Shaula scopes started only after the initial Xids, and neither
  produced a capture file. The same Smithay `glTexSubImage2D` bounds error had
  also occurred twice earlier in the session. The evidence is preserved without
  assigning causation in
  `diagnostics/niri-nvidia-freeze-2026-07-19T2001-0400/`.
- The user session also had an unrelated FortiClient tray crash loop every three
  seconds, adding load and journal noise during validation.
- Repeated `shaula setup --shortcuts --no-integrations` left the Niri config hash
  unchanged.
- `shaula setup` now reloads the live Niri config after writing managed
  keybindings or the Preview rule. The reload path validates the target config
  first, treats IPC failures as advisory, and prints the manual
  `niri validate && niri msg action load-config-file` recovery command when the
  compositor cannot be reloaded automatically.
- `shaula launch` opened the universal GTK capture menu independently from the
  persisted shortcut choice.
- `./dev all` completed a real 1920x1080 `grim-wlroots` all-screens capture with
  clipboard publication, no partial result, no warnings, and no stderr
  diagnostics.
- `./dev capture` launched the real area overlay. This non-interactive execution
  channel could not draw a region, so the test-owned overlay was cancelled;
  process termination and capture-lock cleanup were verified.

GNOME and KDE interactive portal/graphical validation is **not yet checked for
the current implementation**. It is explicitly deferred to v0.1.9 and must not
be reported as passed.

## Release boundary

The `v0.1.8` release commit and tag publish the completed work described above.
GNOME, KDE, optimized archive, and updated AArch64 validation remain scoped to
v0.1.9 and are not claimed by this release. v0.1.9 should also add configurable
capture shortcuts from the Settings view.
