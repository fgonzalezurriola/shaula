# Shaula

Shaula captures, annotates, saves, and copies screenshots on Wayland.

It chooses a working capture route automatically:

- Niri and compatible wlroots compositors use `grim` when it is available.
- Other Wayland desktops use the desktop Screenshot portal and its interactive
  picker.
- Installation stops with an actionable error rather than installing a broken
  application when neither route is available.

Users do not choose a clipboard or global-shortcut backend. Shaula publishes
copied images and text through the required `wl-copy` runtime so selections remain
available after the initiating CLI or Preview process exits. The Settings app
can enable the recommended `Ctrl+Shift+1–4` capture shortcuts with one choice.
Shaula uses the standard XDG GlobalShortcuts portal first. When that portal is
not viable on Niri, installation adds a managed Niri keybinding block after
backing up the existing config. The universal menu remains the fallback
everywhere.

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
interactively when a terminal is available. Required host components are checked
before installation and important missing components are reported in red. It
never invokes `sudo`, installs system packages, or chooses a desktop portal
implementation.

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
Niri/wlroots route. Pacman installs declared dependencies automatically, and the
package hook prints an important red warning when no capture route can be
detected. Niri and Noctalia remain optional integrations.

Optional fonts:

```bash
paru -S ttf-geist ttf-excalifont
```

## Graphical use

The normal application-menu entry runs `shaula launch`, which always opens the
compact capture menu. Global shortcuts are optional and appear first in Settings;
the menu remains available on desktops where automatic shortcuts are unsupported.

The application menu exposes actions for Quick Capture, Capture Area, Capture
Fullscreen, Capture All Screens, and Settings. These actions invoke the capture
commands directly and remain available when automatic global shortcuts are
disabled, declined, or unsupported.

Open Settings directly with:

```bash
shaula settings
```

## Command-line use

```bash
shaula capture quick --json
shaula capture area --json
shaula capture fullscreen --json --save
shaula capture all-screens --json --save
shaula settings --json
shaula shortcuts status --json
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

`shaula setup` remains available for terminal users and automation. Its normal
shortcut question is backend-independent:

```text
Enable Ctrl+Shift+1–4 capture shortcuts? [y/N]
```

Explicit generic flags are available:

```bash
shaula setup
shaula setup --shortcuts
shaula setup --no-shortcuts
shaula setup --niri
shaula setup --noctalia
shaula setup --remove
```

Declining shortcuts is remembered. Shortcut setup and removal are idempotent and
symmetrical. If the XDG GlobalShortcuts portal is unavailable, setup falls back
to a backed-up managed Niri block when Niri is detected. Other desktops retain
the Shaula menu and desktop actions without global shortcuts.

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
./dev install
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
