# Releasing and Installation

This document owns Shaula's release payload, public installer, package metadata,
icons, helpers, and user-integration contracts.

## Ownership model

Immutable application files are owned by Meson, packages, release archives, and
the public installer:

- `shaula` and all bundled helpers, including `shaula-clipboard-provider` and
  `shaula-shortcut-provider`;
- the canonical `data/shaula.desktop` launcher;
- application icons and Preview runtime action icons;
- `data/release-manifest.txt`;
- distributed Noctalia integration source files.

Mutable user state is owned by `shaula setup` and `shaula settings`:

- `${XDG_CONFIG_HOME:-~/.config}/shaula/config.toml`;
- explicit shortcut-choice state;
- the optional shortcut-provider XDG autostart entry and provider status state;
- managed Niri preview and keybinding blocks;
- the installed Noctalia plugin directory and matching Noctalia JSON state.

`./dev` only orchestrates development build, staging, install, setup, validation,
and reload operations. Package hooks must not mutate user configuration.

## GitHub releases

`.github/workflows/release.yml` publishes releases for `v*` tag pushes. Before
building, it rejects the tag unless it is exactly `v` plus the Meson project
version and `docs/release-<tag>.md` exists. The workflow then:

1. Builds and tests the tagged commit in native x86_64 and QEMU-backed native
   AArch64 Ubuntu containers.
2. Stages Meson's install under `usr/bin` and `usr/share` for each architecture.
3. Packages `shaula-linux-x86_64.tar.gz` and
   `shaula-linux-aarch64.tar.gz`.
4. Verifies every executable's ELF architecture and executable mode.
5. Checks that every archive file is manifest-backed and every manifest path is
   present.
6. Confirms that Shaula does not install or claim the shared
   `icons/hicolor/index.theme` file.
7. Writes and verifies one `SHA256SUMS` containing both archives.
8. Exercises installer architecture selection and installs the native archive
   into fake XDG paths.
9. Publishes or replaces the release assets through `gh`, using the matching
   checked-in release notes.
10. Finalizes and pushes `aur/shaula` and `aur/shaula-bin` as `<version>-1` to
    their AUR `master` branches.

The public repository uses `master` as its default branch. The normal installer
command is therefore:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh
```

## Release payload manifest

`data/release-manifest.txt` is the canonical immutable payload list. Meson
installs it as `share/shaula/release-manifest.txt`; release CI, the public
installer, and binary packaging consume the same payload.

Any new helper or runtime resource must be added to Meson and the manifest in the
same change. Release validation must fail when a listed file is absent or a
binary is not executable.

The payload includes:

- `shaula`, overlay, Preview, Settings, crop, portal screenshot,
  shortcut-provider, and clipboard-provider executables;
- the desktop entry with direct capture and Settings actions;
- fixed-size and scalable application icons;
- all Preview symbolic action icons;
- the Noctalia integration source.

## Public installer

`scripts/install.sh` is a user-local installer. Before changing user files it:

1. downloads the release archive and `SHA256SUMS`;
2. verifies the archive checksum;
3. verifies every manifest path, executable mode, and path safety constraint;
4. runs the extracted Shaula binary against the active Wayland session to ensure
   a usable capture route exists.

It then installs the immutable payload, refreshes optional desktop caches, runs
`shaula setup`, and validates the installed capture route, preflight, diagnostics,
clipboard helper, Preview resources, and integration payload.

The installer must never:

- invoke `sudo` or another privilege-escalation command;
- install system packages;
- choose or install a desktop portal backend;
- complete successfully when no usable Screenshot portal or compatible native
  capture route exists.

The desktop environment owns portal backend selection. `grim` is not universal;
it is used automatically only on a compatible compositor when present.

`--yes` is an advanced automation flag. It makes setup noninteractive, enables
the recommended capture shortcuts through the generic backend selector, and
accepts detected optional integrations. It does not authorize system changes or
privilege escalation. Normal installation documentation must not require it.
The public installer also accepts `--shortcuts` and `--no-shortcuts`; the legacy
`--niri-keybinds` flag remains an explicit compatibility alias and conflicts with
`--no-shortcuts`.

`--no-icon` skips application icons only. Preview symbolic icons remain mandatory
runtime resources. `--no-desktop` skips only the canonical desktop entry.

In a noninteractive installation without explicit acceptance, setup creates or
keeps the Shaula config and skips shortcut and optional-integration changes
without failing. With an interactive TTY, setup asks one backend-independent
shortcut question and can separately offer detected Niri window-rule and
Noctalia integration.

A successful installation ends with a direct command the user can run:

```bash
shaula capture area --json
```

## Setup

`shaula setup` owns user configuration and integration state. Supported flags:

- `--yes`
- `--shortcuts`
- `--no-shortcuts`
- `--niri`
- `--noctalia`
- `--niri-keybinds` (compatibility alias for shortcut enablement)
- `--no-integrations`
- `--no-niri`
- `--no-noctalia`
- `--remove`
- `--dry-run`

Setup operations are idempotent. Changed files are validated, backed up, and
atomically replaced. Removal uses the same managed markers and state model as
installation. Interactive setup asks `Enable Ctrl+Shift+1–4 capture shortcuts?
[y/N]`; backend selection remains internal. Decline is persisted so graphical
startup remains independent from shortcut choice state.

### Global shortcuts

`src/shortcuts/` owns generic enable, disable, status, and repair operations for
Settings and CLI setup. The XDG Desktop Portal GlobalShortcuts adapter is
preferred. It is considered usable only after a live provider completes session
creation, binds the four actions, lists the approved mappings, and subscribes to
activation delivery. Desktop remapping and rejection are authoritative.

`shaula-shortcut-provider` exists only to own that live session and dispatch
approved actions directly to existing capture argv arrays. It uses the per-user
D-Bus name `dev.shaula.ShortcutProvider` to prevent duplicate instances and a
bounded reconnect loop after portal restart or session loss. Shaula writes its
XDG autostart entry only after explicit enablement and removes it on disable or
uninstall. Package hooks must never create or remove this user state.

Technical portal non-viability falls back to the managed Niri keybinding adapter.
Desktop rejection does not silently install Niri bindings. If neither path works,
setup succeeds with an unsupported status because desktop launcher actions remain
usable.

Niri integration owns two blocks:

- `// BEGIN/END SHAULA PREVIEW WINDOW RULE`
- `// BEGIN/END SHAULA MANAGED KEYBINDS`

Noctalia integration installs only into a directory marked `.shaula-managed`.
It refuses to overwrite or remove an unmarked plugin directory. When present,
`plugins.json` must use version 2 with an object `states` member, and
`settings.json` must expose `bar.widgets`; otherwise setup fails without applying
that mutation. Missing Noctalia JSON files are reported as skipped rather than
invented.

## Clipboard publication

Capture, `shaula clipboard`, and Preview publish through one clipboard module.
The module pipes complete PNG or UTF-8 payloads to the required `wl-copy`
runtime, which uses Wayland data-control and keeps the selection available after
the initiating process exits. Spawn and nonzero-exit failures map through the
existing deterministic clipboard status and `ERR_*` taxonomy. Preview paste
continues to use asynchronous GTK/GDK reads.

## Capture packaging contract

Runtime capability selection advertises only verified prerequisites:

- `grim-wlroots` requires a resolvable `grim` executable on Niri or a compatible
  wlroots compositor;
- `portal-screenshot` requires a verified
  `org.freedesktop.portal.Screenshot` interface;
- otherwise preflight and installation fail with a deterministic unavailable
  capture route.

Portal Quick/Area capture invokes the portal's interactive picker directly. It
must not prepare a frozen source, launch `shaula-overlay`, or pass a Shaula area
geometry to the portal helper. Portal cancellation, timeout, unavailability, and
other failure outcomes remain distinct.

## Local development installation

`./dev dev-install [installer args...]` builds the current checkout, stages the
Meson install, creates a local release archive and checksums, invokes the public
installer with `file://` assets, runs setup and validation, and reloads Noctalia
when applicable.

```bash
./dev dev-install --yes
```

The command displays build, staging, install, setup, and validation phases.
`--yes` accepts the recommended shortcuts. To exercise an explicit decline in a
local install, run setup afterward with `shaula setup --no-shortcuts`; the legacy
`--niri-keybinds` option remains available for compatibility testing.

`./dev noctalia-load` is widget-only: it builds Shaula, runs setup with Niri
disabled and Noctalia explicitly selected, then reloads Noctalia. It does not run
the full installer.

## AUR packages

Packaging scaffolds live in `aur/shaula` and `aur/shaula-bin`.

Both packages declare linked application libraries, JSON-GLib, `wl-clipboard`,
and the desktop portal framework as hard dependencies. `grim`, Niri, and
Quickshell are classified as optional integrations/capture implementations.

`shaula` builds from source for `x86_64` and `aarch64`. `shaula-bin` consumes
the checked x86_64 or AArch64 release archive selected by makepkg. Both
`.SRCINFO` files must be regenerated whenever their PKGBUILD metadata changes.

The binary package installs the complete archive payload, including
`shaula-clipboard-provider` and `shaula-shortcut-provider`, rather than recreating
the desktop entry or icons.

### Automated AUR publication

The checked-in PKGBUILDs must retain every `SKIP` marker. They are release
preparation templates because tag sources and release assets do not exist before
the tag workflow runs. `scripts/qa/assert-release-contract.sh` enforces this
state, and contributors must not finalize checksums in the source repository.

After publishing the GitHub release, `.github/workflows/release.yml` performs the
only canonical AUR publication flow:

1. read both binary archive hashes from the already verified `SHA256SUMS` and
   derive the package version from the validated tag;
2. download the immutable tag source archive and tagged `LICENSE`, then calculate
   their SHA-256 hashes;
3. clone `ssh://aur@aur.archlinux.org/shaula.git` and `shaula-bin.git` into
   writable temporary directories;
4. copy each checked-in package template into its clone, replace the markers with
   the matching hashes, and regenerate `.SRCINFO` there with
   `makepkg --printsrcinfo` in an Arch container;
5. commit `<version>-1` metadata when it changed and push `HEAD` to each AUR
   repository's `master` branch.

The workflow uses the no-passphrase deploy key stored in the `AUR_SSH_KEY`
Actions secret (the local counterpart is `~/.ssh/id_aur`, comment
`shaula-aur-deploy`). It records the `aur.archlinux.org` Ed25519 host key and
requires `StrictHostKeyChecking`. A tag release fails if either AUR package
cannot be finalized or pushed; there is no manual checksum or publication step.
The temporary AUR clones must contain no `SKIP` marker when pushed, while the
source repository must continue to contain them.

## Icon packaging

The desktop launcher uses `Icon=shaula`. Application icons are distributed at
48, 64, 128, 256, and 512 pixels plus
`scalable/apps/shaula.svg`. Preview runtime icons are installed under
`scalable/actions`.

Shaula installs its icon files into the existing hicolor tree but never installs
or overwrites the shared `index.theme`. Cache refresh tools are optional and must
not determine installation success.

## Verification

After every code change:

```bash
./dev check
git diff --check
```

For release/install changes also run:

```bash
scripts/qa/assert-release-contract.sh "$PWD"
scripts/qa/assert-release-archive.sh "$PWD" \
  dist/shaula-linux-x86_64.tar.gz x86_64
scripts/qa/assert-release-archive.sh "$PWD" \
  dist/shaula-linux-aarch64.tar.gz aarch64
./dev dev-install --yes
```

Wayland capture, shortcut activation, Settings presentation, and clipboard
lifetime still require a live graphical session. For native interactive behavior
run:

```bash
./dev capture
./dev all
shaula shortcuts status --json
niri validate -c "${XDG_CONFIG_HOME:-$HOME/.config}/niri/config.kdl"
```

On Niri, reload the config and verify at least one real managed shortcut event.
Record whether the event was generated by a physical keyboard or an input
injector, and record the resulting capture path. Verify that `shaula launch`
opens the capture menu with an isolated XDG config/state profile, regardless of
shortcut choice state.

On hybrid NVIDIA systems, also inspect the kernel journal for `NVRM: Xid` after
manual graphical checks and record which DRM connector belongs to each GPU. If
an NVIDIA-owned output freezes or `nvidia-smi` loses the device handle, stop
interactive validation and record it as a GPU/driver environment failure unless
timestamps and a repeatable test establish a Shaula causal path. Do not infer
causality from a delayed compositor failure alone.

GNOME and KDE interactive portal/graphical validation is not checked for
v0.1.6. It is explicitly deferred to v0.1.7 and must not be reported as passed.
