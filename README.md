# Shaula

Shaula is a screenshot tool for Wayland/Niri.

Shaula is currently tested primarily on Niri. It also includes integration work
for Noctalia Shell. Broader Wayland compositor support is in progress, but Niri
is the main supported environment right now.

![Shaula preview with annotations](docs/assets/shaula-preview.png)

## Installation

Install or update with:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh
```

Install a specific release with:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --version v1.0.0
```

For a non-interactive install:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --yes
```

Uninstall with:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --uninstall
```

## Runtime Requirements

Shaula currently expects Wayland and is tested mainly on Niri. Full support
across GNOME, KDE, Hyprland, Sway, and other Wayland compositors is not promised
yet.

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

## Fonts

Shaula uses **Geist** (normal) and **Excalifont** (sketch).

```bash
paru -S ttf-geist ttf-excalifont
```

## Usage

Main usage is tied with the Noctalia-Shell menu installed. Shaula can be call through the terminal

```bash
shaula capture quick --json
shaula capture area --json
shaula capture area --json --no-preview
```

## Development

Requirements:

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
./dev noctalia-load
./dev dev-install
```

## Support

<a href="https://ko-fi.com/fgonzalezurriola">
  <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi">
</a>
