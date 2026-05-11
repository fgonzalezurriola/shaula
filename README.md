# Shaula

Shaula is a fast screenshot tool for Wayland/Niri, built for a Shottr-like
workflow on Linux.

It focuses on quick area capture, a native selection overlay, post-capture
preview/editing, and scriptable JSON output.

> Shaula's main target is CachyOS/Arch + Niri.

## Install

Shaula installs locally for the current user. The installer does not use
`sudo`.

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh
```

Uninstall:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh -s -- --uninstall
```

Advanced install commands:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh -s -- --yes
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh -s -- --version v1.0.0
```

The installer:

- installs under `~/.local` and `~/.config`
- verifies GitHub release `SHA256SUMS`
- does not use `sudo`
- does not overwrite an existing `~/.config/shaula/config.toml`
- does not edit Niri config automatically
- can optionally install the Noctalia Bar Widget with config backups

If Niri is detected, the installer generates
`~/.config/shaula/generated/niri-shaula.kdl` for manual inclusion in your active
Niri config.

## Features

- Area, fullscreen/current-monitor, all-screens, window, and previous-area capture
- Native GTK/layer-shell selection overlay
- Post-capture preview/editor
- Native settings window for the current capture and preview-window config
- Copy, Save As, Crop, Blur, Erase, Spotlight, annotations, undo/redo
- Live hover color sampling and Tab-to-copy color
- JSON output for automation
- Niri-first behavior with conservative integration helpers
- Optional Noctalia Bar Widget that calls Shaula CLI commands

Not implemented yet:

- OCR
- screen recording
- scrolling capture
- pin screenshot

## Runtime Requirements

Shaula currently expects a Wayland compositor and is tested mainly on Niri.

Recommended runtime tools:

- `grim`
- `wl-clipboard` / `wl-copy`
- GTK4 / gtk4-layer-shell runtime libraries

Optional integration tools:

- `slurp`, only if needed as a fallback selection helper
- `niri`, recommended for the best integration
- `quickshell`, only for Noctalia integration

On Arch/CachyOS:

```bash
sudo pacman -S grim wl-clipboard gtk4 gtk4-layer-shell
```

## Usage

```bash
shaula doctor
shaula doctor --json
shaula capture area --json
shaula capture area --json --no-preview
shaula capture fullscreen --json --preview
shaula capture all-screens --json
shaula capture previous-area --json
shaula preview ~/Pictures/Shaula/example.png --json
shaula settings
```

## Development Requirements

- Zig 0.16.0
- `jq`
- GTK4 / gtk4-layer-shell development packages
- Wayland development packages

Build from source:

```bash
zig build
```

Run checks:

```bash
./dev check
```

Useful development commands:

```bash
./dev capture
./dev frozen
./dev context
```

## Project Docs

- `DEV.md`: development workflow and integration notes
- `CONTEXT.md`: current implementation snapshot
- `docs/roadmap.md`: planned features
- `spec/`: contracts and architecture notes
- `scripts/qa/`: QA checks
