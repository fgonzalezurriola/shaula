# Shaula v0.1.6

Shaula v0.1.6 completes the Meson/C production port and makes the release
artifact, installer, clipboard publication, and Wayland backend contracts explicit.
It remains a short-lived CLI/helper application: no resident Shaula daemon is
introduced.

## Highlights

- Production, helpers, tests, packaging, and CI now use the Meson/C tree.
- Capture route selection supports native `grim` on Niri/wlroots and the
  Screenshot portal on other Wayland desktops, with deterministic no-route
  errors.
- Clipboard publication uses the required `wl-copy` runtime so CLI capture can
  acquire the Wayland selection without an input-event serial and keep PNG or
  UTF-8 text available after Shaula exits.
- Missing, failed, and unavailable clipboard publication is deterministically
  classified through Shaula's existing status and `ERR_*` taxonomy.
- Release archives are produced for both x86_64 and AArch64:
  - `shaula-linux-x86_64.tar.gz`
  - `shaula-linux-aarch64.tar.gz`
- The public installer chooses the archive from `uname -m`, verifies the shared
  `SHA256SUMS`, validates the manifest-backed payload, and preserves executable
  modes.
- Both AUR package definitions are prepared for `0.1.6`; the binary package
  supports x86_64 and AArch64.

## Release artifact contract

Each archive contains exactly the files listed by
`share/shaula/release-manifest.txt`, plus the manifest itself, under the two
expected top-level directories: `bin` and `share`.

Release validation checks:

- tag equality with the Meson project version;
- existence of `docs/release-v0.1.6.md`;
- both architecture archives and their checksums;
- ELF architecture for every installed executable;
- manifest completeness and absence of unlisted files;
- executable modes for every `bin/*` entry;
- absence of `share/icons/hicolor/index.theme`;
- installer architecture selection;
- regenerated AUR `.SRCINFO` consistency.

## AUR preparation state

The AUR PKGBUILDs deliberately use `SKIP` while the `v0.1.6` source archive and
release assets do not yet exist. `SKIP` is a visible release-preparation marker,
not a publishable final checksum. Before publishing either AUR package, replace
those markers with the immutable checksums produced after the tag and GitHub
release assets exist, regenerate both `.SRCINFO` files, and rerun the release
contract checks described in `docs/releasing.md`.

## Validation scope

Automated and local release checks cover the C build, unit and contract tests,
clipboard lifecycle cleanup, multi-process replacement ordering, staged release
archives, x86_64/AArch64 executable architecture, installer behavior, and AUR
metadata consistency.

Live Niri checks remain the primary graphical release environment. GNOME and KDE
interactive portal/graphical validation is **not yet checked for v0.1.6** and is
explicitly deferred to v0.1.7. This release does not claim that those graphical
checks passed.
