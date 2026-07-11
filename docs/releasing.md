# Releasing and Installation

This document owns Shaula's release, installer, package, icon, and user-integration contracts.

## GitHub Releases

GitHub Releases are published by `.github/workflows/release.yml` on `v*` tag pushes.

The release job:

1. Configures and builds the tagged commit with Meson in release mode.
2. Runs the maintained Meson tests.
3. Stages the Meson install and packages its `usr/bin` and `usr/share` payload as `shaula-linux-x86_64.tar.gz`.
4. Writes and verifies `SHA256SUMS`.
5. Verifies that the archive contains every helper binary, preview toolbar icon, and Noctalia widget file.
6. Installs the local archive into fake XDG paths to validate desktop, icon, config, Niri, and Noctalia behavior.
7. Publishes or replaces release assets through `gh`.

The publish job uses `contents: write`, does not run on pull requests, does not use shared caches, and uses a tag-specific run name.

The public repository currently uses `master` as its default branch. Raw installer links must therefore use `raw.githubusercontent.com/.../master/scripts/install.sh` unless the default branch changes.

## Release Installer

`scripts/install.sh` and `scripts/uninstall.sh` provide a user-local installation flow.

The installer:

- verifies the GitHub Release `SHA256SUMS`;
- supports the latest stable release, `--version`, a positional `v*` version, and `SHAULA_VERSION`;
- warns about missing runtime tools;
- installs desktop and icon assets;
- refreshes the user hicolor icon cache and desktop application database when
  `gtk-update-icon-cache` and `update-desktop-database` are available;
- delegates user configuration and integrations to `shaula setup`;
- preserves an existing `~/.config/shaula/config.toml`;
- uses `sudo` only after explicit Arch/CachyOS runtime-dependency confirmation.

On Arch/CachyOS, the dependency prompt installs:

```bash
sudo pacman -S --needed grim wl-clipboard gtk4 gtk4-layer-shell
```

The prompt reads from `/dev/tty` so an interactive `curl | sh` install still works. It logs when packages are already installed and uses concise TTY status prefixes without coloring entire log lines. `--yes` must never authorize privilege escalation automatically.

Test-only installer variables:

- `SHAULA_INSTALL_ASSUME_ARCH=1`
- `SHAULA_INSTALL_TEST_MISSING_ARCH_PACKAGES=...`

These variables allow the dependency prompt to be tested without uninstalling system packages.

Release installs otherwise stay non-interactive. `shaula setup` owns optional Niri and Noctalia prompts when those environments are detected.

Noctalia plugin state hot-applies. The release installer must not ask users to restart Noctalia after installing the widget.

Every prompt that changes integration state must explain the visible outcome, affected files, settings, or keybindings before requesting confirmation. User-facing copy should describe outcomes rather than expose internal CLI commands. Output should be grouped by visible phases and name backup/config paths when useful for recovery.

## Local Development Installation

`./dev dev-install [scripts/install.sh args...]` builds the current checkout, stages the Meson install into a temporary release archive with `SHA256SUMS`, and runs `scripts/install.sh` against `file://` URLs.

Use:

```bash
./dev dev-install --yes
```

for a non-interactive install of the exact local build. The old `./dev install` alias no longer exists.

The development wrapper sets `SHAULA_INSTALL_CONTEXT=dev` and makes install/Noctalia prompts explicit that the current local build is being installed or reloaded rather than a GitHub release.

When `~/.config/noctalia` exists, a real development install restarts Noctalia by trying, in order:

1. `noctalia.service`
2. `qs`
3. `quickshell`

Restart is skipped for `--help`, `--uninstall`, and `--no-integrations`.

`./dev dev-install --yes` does not install Niri keybindings by itself. It passes `--skip-niri-keybinds` to `shaula setup` so non-interactive installs cannot silently edit Niri configuration.

Install keybindings explicitly with one of:

```bash
./dev dev-install --yes --niri-keybinds
shaula setup
```

or the Settings shortcut installer.

Managed Ctrl+Shift+1/2/3/4 Niri keybindings must spawn `shaula capture <mode> --json`. Capture commands reject non-JSON invocations with `ERR_CLI_USAGE`.

## Setup Wizard

`shaula setup` is the post-install user wizard for package-manager installations such as AUR `shaula-bin`.

Packages install files. Setup owns user state:

- creates the user config;
- asks before mutating Niri configuration;
- asks before installing the Noctalia Bar Widget;
- writes backups before editing JSON or config files.

Supported non-interactive/setup flags include:

- `--yes`
- `--no-integrations`
- `--no-niri`
- `--no-noctalia`
- `--niri-keybinds`
- `--skip-niri-keybinds`
- `--dry-run`

## AUR Packages

Packaging scaffolds live under:

- `aur/shaula`
- `aur/shaula-bin`

`shaula` builds from source for `x86_64` and `aarch64`; Zig belongs only in `makedepends`.

`shaula-bin` installs the GitHub Release archive without Zig. It currently declares only `x86_64` because releases do not yet provide a checked `shaula-linux-aarch64.tar.gz` asset.

Both packages install a post-install message that points users to Settings and conditionally to `shaula setup`. Package hooks must not mutate user configuration.

AUR maintainer metadata uses:

```text
fgonzalezurriola <fgonzalezurriola@gmail.com>
```

Keep `shaula` and `shaula-bin` aligned when publishing releases. Both scaffolds are currently at v0.1.5 with regenerated `.SRCINFO` and their corresponding source/binary checksums.

## Icon Packaging

The installer copies the packaged `share/icons/hicolor` tree into `~/.local/share/icons/hicolor`.

The desktop launcher uses `Icon=shaula`. The application icon is distributed as fixed PNG sizes under:

- `48x48`
- `64x64`
- `128x128`
- `256x256`
- `512x512`

It is also distributed as a raster-preserving `scalable/apps/shaula.svg` so
launchers that prefer scalable app icons, including Noctalia/Quickshell, can
resolve `Icon=shaula` without changing the app artwork.

The source raster is `src/preview/icons/source/shaula-source.png`. Regenerate
the scalable app SVG from that source with Inkscape. Do not add unused
root-level icon copies.

Uninstall removes these PNG sizes and the SVG application icon, then refreshes
the same desktop caches when the host tools are available.

Preview toolbar icons are loaded from `../share/icons` relative to the installed helper. Missing `scalable/actions/shaula-*-symbolic.svg` assets appear as GTK missing-icon glyphs and must be caught by release archive/install verification.

## Niri and Noctalia Integration

Integration state is user-scoped. Packages and the installer place files; `shaula setup` detects user configuration and asks before changing it.

Managed Niri blocks include:

- the preview floating window rule;
- quick capture;
- area capture;
- current-monitor fullscreen capture;
- all-screens capture.

Generated keybindings use the installed absolute `shaula` path.

When Noctalia is detected, setup can install `integrations/noctalia/shaula/` into `~/.config/noctalia/plugins/shaula/`. It enables `states.shaula.enabled` and adds `plugin:shaula` to a bar only after backing up and validating Noctalia JSON configuration.

The Noctalia menu exposes capture actions, Settings, screenshots folder, and bug reporting. `shaula doctor` stays out of the menu because it is a diagnostic/development command.

Niri and Noctalia detection must honor `XDG_CONFIG_HOME` before legacy `~/.config/...` fallbacks. This applies to release installs and CI install-smoke tests.
