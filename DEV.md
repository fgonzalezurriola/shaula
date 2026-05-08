# Shaula Development

This file keeps contributor-facing workflow notes out of the public README.

## Local Build

```bash
zig build
./dev check
```

Useful commands:

```bash
./dev capture
./dev frozen
./dev context
./dev doctor
./dev strategies
./dev bench
```

## Runtime Setup

Shaula is developed primarily against Wayland/Niri.

```bash
export SHAULA_COMPOSITOR=niri
export NIRI_SOCKET=/run/user/1000/niri-0.sock
```

Common manual commands:

```bash
./zig-out/bin/shaula preflight --json
./zig-out/bin/shaula capture area --json
./zig-out/bin/shaula capture area --json --no-preview
./zig-out/bin/shaula capture fullscreen --json --preview
./zig-out/bin/shaula capture all-screens --json
./zig-out/bin/shaula capture previous-area --json
./zig-out/bin/shaula preview ~/Pictures/Shaula/example.png --json
```

## Installer Details

The installer downloads releases from
`https://github.com/fgonzalezurriola/shaula`, verifies `SHA256SUMS`, and works
only with user-local paths.

Installed paths:

- `~/.local/bin/shaula`
- `~/.local/bin/shaula-overlay`
- `~/.local/bin/shaula-preview`
- `~/.local/share/applications/shaula.desktop`
- `~/.local/share/icons/hicolor/scalable/apps/shaula.svg`
- `~/.config/shaula/config.toml`
- `~/.config/shaula/generated/`

The installer detects `x86_64` and `aarch64`, warns if runtime tools are
missing, and never overwrites an existing `~/.config/shaula/config.toml`.

## Niri Integration

The installer does not edit Niri config automatically.

If Niri is detected, it generates:

```txt
~/.config/shaula/generated/niri-shaula.kdl
```

You can manually include or copy that snippet into your active Niri config.

Manual helper commands:

```bash
shaula config show --json
shaula config init --json
shaula config niri-window-rule --json
```

`shaula config niri-install --json` can edit a marked Shaula block in
`~/.config/niri/config.kdl`, but this is an explicit command and is separate
from the installer.

## Noctalia

Plugin source lives in:

```txt
integrations/noctalia/shaula/
```

Installed Noctalia paths:

- `~/.config/noctalia/plugins/shaula/`
- `~/.config/noctalia/plugins.json`
- `~/.config/noctalia/settings.json`

Install from the script and accept the Noctalia prompt:

```bash
scripts/install.sh --yes
```

For local development, use the dev helper. It builds the current checkout,
installs the local widget through the checksum-verified installer path, and
restarts the user Noctalia service:

```bash
./dev noctalia-load
```

The installer copies the plugin files, writes a `.shaula-managed` marker, backs
up Noctalia JSON files as `*.shaula-backup-<timestamp>`, enables
`states.shaula.enabled`, and adds `plugin:shaula` to the right bar section when
the local JSON structure matches Noctalia's v2 registry format.

If JSON validation fails, the installer leaves the copied plugin files in place
and prints manual instructions. Manual enable is:

```txt
Enable shaula in Noctalia plugin settings.
Add plugin:shaula to a bar section if Noctalia does not add it automatically.
```

Debug commands:

```bash
shaula doctor
shaula doctor --json
quickshell log
```

Noctalia context menu behavior: the base menu closes almost immediately after a
menu item is clicked, so the Shaula plugin should not add artificial capture
delays unless a future Noctalia version regresses menu dismissal.

Uninstall:

```bash
scripts/install.sh --uninstall
```

Uninstall removes `~/.config/noctalia/plugins/shaula/` only when the
`.shaula-managed` marker exists, and removes only Shaula's `plugins.json` state
and `plugin:shaula` bar widget entry after creating backups.
