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
./zig-out/bin/shaula capture all-in-one --json
./zig-out/bin/shaula capture fullscreen --json --preview
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

The installer detects Noctalia paths, but does not modify `plugins.json`.

The real Noctalia Bar Widget is not implemented yet.
