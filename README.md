# Shaula

Shaula is a Wayland screenshot application with capture, area selection, Preview,
saving, and clipboard copy as built-in product features.

It chooses a working capture route automatically:

- Niri and compatible wlroots compositors use `grim` when it is available.
- Other Wayland desktops use the desktop Screenshot portal and its interactive
  picker.
- Installation stops with an actionable error rather than installing a broken
  application when neither route is available.

Users do not choose a clipboard backend. Shaula publishes copied images and text
through the required `wl-copy` runtime so selections remain available after the
initiating CLI or Preview process exits.

[![Shaula demo](docs/assets/shaula-demo.gif)](docs/assets/demo-readme.mp4)

## Current release

The latest stable release is **v0.1.6**. See
[`docs/release-v0.1.6.md`](docs/release-v0.1.6.md) for that release's highlights
and validation notes.

## Installation

Run the normal installer from an active Wayland desktop session:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh
```

The installer verifies the complete release before changing user files, installs
under `~/.local`, validates the current capture route, and runs optional setup
interactively when a terminal is available. It never invokes `sudo`, installs
system packages, or chooses a desktop portal implementation.

Uninstall:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --uninstall
```

### Arch Linux / CachyOS

```bash
paru -S shaula      # or: paru -S shaula-bin
shaula setup
```

The packages declare linked application libraries, `wl-clipboard`, and the
desktop portal framework. `grim` is optional and enables the native
Niri/wlroots route. Niri and Noctalia remain optional integrations.

Optional fonts:

```bash
paru -S ttf-geist ttf-excalifont
```

## Usage

```bash
shaula capture quick --json
shaula capture area --json
shaula capture fullscreen --json --save
shaula capture all-screens --json --save
shaula settings --json
shaula explore --json --brief
```

Native Niri/wlroots area capture uses Shaula's overlay. Portal-based area capture
uses the desktop's picker directly and does not launch Shaula's overlay.

Preview supports Copy, Save, Save As, and Done. Saved screenshots default to
`~/Pictures/shaula` and use `YYYYMMDD-HHMMSS.png`, adding `-2`, `-3`, and so on
when necessary. `Ctrl+Shift+V`, also available as **Paste text/image**, reads the
system clipboard through GTK/GDK and inserts text or an image into the canvas.

Inside Shaula's native Quick/Area overlay:

- `Enter` follows the configured capture flow.
- `Ctrl+C` captures and copies without opening Preview.
- `Ctrl+S` captures and saves without opening Preview.

Optional integrations can be configured or removed symmetrically. Interactive
setup asks separately about the Niri window rule, recommended capture shortcuts,
and the Noctalia widget:

```bash
shaula setup
shaula setup --niri --niri-keybinds
shaula setup --noctalia
shaula setup --remove
```

## Development

Requirements:

- Meson and Ninja
- A C11 compiler
- `jq`
- GTK4, gtk4-layer-shell, GDK Pixbuf, Cairo, Pango, and JSON-GLib development
  packages
- Wayland development packages

Build and test:

```bash
meson setup build --prefix=/usr
meson compile -C build
./dev check
```

Useful development commands:

```bash
./dev dev-install --yes
./dev capture
./dev all
./dev noctalia-load
./dev run capabilities list --json
```

`./dev noctalia-load` changes only Noctalia user state and reloads Noctalia; it
does not perform a full application install.

## Support

<a href="https://ko-fi.com/fgonzalezurriola">
  <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi">
</a>

## License

MIT License. See [LICENSE](LICENSE).
