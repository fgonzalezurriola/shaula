# Releasing and Installation

This document owns Shaula's release payload, public installer, package metadata,
icons, helpers, and user-integration contracts.

## Ownership model

Immutable application files are owned by Meson, packages, release archives, and
the public installer:

- `shaula` and all bundled helpers, including `shaula-clipboard-provider`;
- the canonical `data/shaula.desktop` launcher;
- application icons and Preview runtime action icons;
- `data/release-manifest.txt`;
- distributed Noctalia integration source files.

Mutable user state is owned by `shaula setup`:

- `${XDG_CONFIG_HOME:-~/.config}/shaula/config.toml`;
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

- `shaula`, overlay, Preview, Settings, crop, portal, and clipboard-provider
  executables;
- the desktop entry;
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

`--yes` is an advanced automation flag. It makes setup noninteractive and accepts
detected optional integrations, but it does not authorize system changes or
privilege escalation. Normal installation documentation must not require it.

`--no-icon` skips application icons only. Preview symbolic icons remain mandatory
runtime resources. `--no-desktop` skips only the canonical desktop entry.

In a noninteractive installation without explicit acceptance, setup creates or
keeps the Shaula config and skips optional integrations without failing. With an
interactive TTY, setup can offer detected Niri and Noctalia integration.

A successful installation ends with a direct command the user can run:

```bash
shaula capture area --json
```

## Setup

`shaula setup` owns user configuration and integration state. Supported flags:

- `--yes`
- `--niri`
- `--noctalia`
- `--niri-keybinds`
- `--no-integrations`
- `--no-niri`
- `--no-noctalia`
- `--remove`
- `--dry-run`

Setup operations are idempotent. Changed files are validated, backed up, and
atomically replaced. Removal uses the same managed markers and state model as
installation.

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
Keybindings remain opt-in:

```bash
./dev dev-install --yes --niri-keybinds
```

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
`shaula-clipboard-provider`, rather than recreating the desktop entry or icons.

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

Wayland capture and clipboard lifetime still require a live graphical session.
For native interactive behavior run:

```bash
./dev capture
./dev all
```

GNOME and KDE interactive portal/graphical validation is not checked for
v0.1.6. It is explicitly deferred to v0.1.7 and must not be reported as passed.
