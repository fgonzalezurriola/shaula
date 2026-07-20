# Shaula v0.1.6

Shaula v0.1.6 completes the Meson/C production port and makes the release
artifact, installer, clipboard publication, graphical setup, and Wayland backend
contracts explicit. Capture, Preview, Settings, and CLI commands remain
short-lived. One narrowly scoped optional resident helper is introduced only to
own an approved GlobalShortcuts portal session.

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
- `shaula settings` places optional **Global Shortcuts** first, with simplified
  status, enable/disable/repair controls, and access to the universal menu.
  Noctalia remains in the separate **Integrations** section.
- Graphical launch always opens the universal capture menu and never waits for
  shortcut setup or desktop support.
- One backend-independent shortcut choice requests `Ctrl+Shift+1–4`. Shaula
  uses a verified XDG GlobalShortcuts portal session and reports non-viability
  without modifying compositor keybindings.
- `shaula-shortcut-provider` owns the optional live portal session, prevents
  duplicate instances and captures, reconnects after session loss, dispatches
  capture commands without a shell, and is started through user-owned XDG
  autostart only after explicit enablement.
- The desktop entry provides direct actions for Quick Capture, Capture Area,
  Capture Fullscreen, Capture All Screens, and Settings.

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
- inclusion and executable mode of `shaula-shortcut-provider`;
- direct desktop launcher actions and universal `shaula launch` menu entry;
- installer architecture selection;
- regenerated AUR `.SRCINFO` consistency.

## AUR state

Both AUR packages are published as `0.1.6-1` with final immutable checksums and
regenerated `.SRCINFO` files. `shaula` builds from the `v0.1.6` source archive;
`shaula-bin` installs the matching x86_64/AArch64 release archive. `wl-clipboard`
is a required runtime dependency. AUR publication used the dedicated deploy key
`~/.ssh/id_aur` (`shaula-aur-deploy`), independent from the personal
`id_ed25519` key. No `SKIP` markers remain.

## Validation scope

Automated and local release checks cover the C build, unit and contract tests,
clipboard lifecycle cleanup, multi-process replacement ordering, generic
portal shortcut lifecycle, unsupported fallback, shortcut-choice persistence,
provider duplicate/reconnect contracts, Settings mapping, desktop actions,
staged release archives, installer behavior, and AUR payload consistency.

Live Niri checks remain the primary graphical release environment. The current
checkout was installed with `./dev dev-install --yes`; portal-first selection
reported the unavailable portal without leaving provider autostart state.
`niri validate` and live config reload succeeded, repeated shortcut enablement
was byte-for-byte idempotent, and `shaula launch` opened the universal capture menu,
and a compositor-level `Ctrl+Shift+4` event injected through `ydotool` created
`/home/fgonz/Pictures/shaula/20260719-234615.png` at 19:46:15 local time.

At 20:01:21, over 15 minutes after the capture completed, the NVIDIA driver
began reporting Xids 56 and 11 against Niri. The sequence progressed through a
GSP timeout to Xid 79 (`GPU has fallen off the bus`) at 20:03:23. The
NVIDIA-owned HDMI output froze while the AMD-owned laptop panel and Niri IPC
remained responsive; `nvidia-smi` could no longer obtain a device handle. Two
later Shaula scopes started only after the initial Xids, and neither produced a
capture file. The same Smithay OpenGL texture-bounds error had occurred twice
earlier in the session, and an unrelated FortiClient tray process was
crash-looping every three seconds. The evidence is preserved without assigning
causation in `diagnostics/niri-nvidia-freeze-2026-07-19T2001-0400/`.

GNOME and KDE interactive portal approval, remapping, activation, and graphical
validation are **not yet checked for v0.1.6** and are explicitly deferred to
v0.1.7. The updated provider payload was also not rebuilt for AArch64 in this
checkout because `SHAULA_MESON_CROSS_FILE` was not configured; native/QEMU
AArch64 release validation remains required. This release does not claim that
those checks passed.
