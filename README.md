# Shaula

Shaula is a screenshot tool for Wayland/Niri.

Shaula is currently tested primarily on Niri. It also includes integration work
for Noctalia Shell. Broader Wayland compositor support is in progress, but Niri
is the main supported environment right now.

https://github.com/fgonzalezurriola/shaula/raw/master/docs/assets/shaula-demo.mp4

## Installation

Install or update with:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh
```

On Arch/CachyOS, the installer can prompt to install missing runtime packages
with `sudo pacman -S --needed ...` when it detects a TTY.

To test that prompt without uninstalling packages, answer `n`:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | SHAULA_INSTALL_TEST_MISSING_ARCH_PACKAGES=grim sh
```

Uninstall with:

```bash
curl -fsSL https://raw.githubusercontent.com/fgonzalezurriola/shaula/master/scripts/install.sh | sh -s -- --uninstall
```

## Dependencies

Install runtime dependencies:

```bash
sudo pacman -S --needed grim slurp wl-clipboard gtk4 gtk4-layer-shell
```

Install fonts dependencies:

```bash
paru -S ttf-geist ttf-excalifont
```

## Usage

Main usage is tied to the installed Noctalia Shell menu and by shortcuts (Ctrl+Shift+1/2/3/4). 

Shaula can also becalled through the terminal:

```bash
shaula capture quick --json
shaula capture area --json
shaula capture area --json --no-preview
```

Preview supports Copy, Save, Save As, and Done/accept flows. Save and Done use
the configured save folder, defaulting to `~/Pictures/shaula`, and generate
`shaula-screenshot-YYYYMMDD-HHMMSS.png` names from the preview. Direct
no-preview saved captures use `shaula-<mode>-<milliseconds>.png`. The default
fullscreen and all-screens shortcuts save a durable copy to that folder.

## Development

Requirements:

- Zig 0.16.0
- `jq`
- GTK4 / gtk4-layer-shell development packages
- Wayland development packages

The Zig version is pinned in `.tool-versions`, CI, and
`scripts/qa/check-zig-version.sh`. Use exactly Zig 0.16.0 for release builds
unless the pin is updated everywhere in one change.

Build from source:

```bash
zig build
```

Release build:

```bash
zig build -Doptimize=ReleaseSafe -Dstrip
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

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

MIT License

Copyright (c) 2026 Fernando González Urriola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
