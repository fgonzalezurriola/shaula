# Shaula

Shaula is a fast screenshot tool for Wayland/Niri, built for a Shottr-like
workflow on Linux.

> Shaula is early software. The main target is CachyOS/Arch + Niri.

## Install

Shaula can be installed locally without sudo:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh
```

Uninstall:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/main/scripts/install.sh | sh -s -- --uninstall
```

The installer verifies GitHub release `SHA256SUMS`, installs into user-local
paths under `~/.local` and `~/.config`, never uses `sudo`, and does not
overwrite an existing `~/.config/shaula/config.toml`.

If Niri is detected, the installer generates
`~/.config/shaula/generated/niri-shaula.kdl` for manual inclusion. It does not
edit your Niri config automatically. It also detects Noctalia, but does not
modify `plugins.json` or install a Bar Widget yet.

## Features

- Area, fullscreen, focused-output, window, all-in-one, and previous-area capture
- Native GTK/layer-shell selection overlay
- Post-capture preview/editor
- Copy, Save As, Crop, Blur, Erase, Spotlight, annotations, undo/redo
- Live hover color sampling and Tab-to-copy color
- JSON output for automation
- Niri-first behavior with conservative integration helpers

Not implemented yet:

- OCR
- screen recording
- scrolling capture
- pin screenshot
- Noctalia Bar Widget

## Requirements

Shaula currently expects a Wayland desktop and is tested mainly on Niri.

Recommended runtime tools on Arch/CachyOS:

```bash
sudo pacman -S grim wl-clipboard
```

Optional integration tools:

```bash
sudo pacman -S slurp niri
```

## CLI Usage

```bash
shaula capture area --json
shaula capture area --json --no-preview
shaula capture fullscreen --json --preview
shaula capture previous-area --json
shaula preview ~/Pictures/Shaula/example.png --json
```

## Development

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
./dev state
```

Development requirements:

```bash
sudo pacman -S zig jq gtk4 gtk4-layer-shell wayland
```

- Zig 0.16.0
- `jq`
- GTK4 / gtk4-layer-shell development packages
- Wayland development packages
