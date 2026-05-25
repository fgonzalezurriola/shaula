# Shaula Configuration

Shaula uses a file-first configuration model. The first supported public config
surface is preview-window intent for Niri users.

Shaula does not reload Niri or mutate windows after spawning them. Niri already
owns window placement through `window-rule`. Shaula keeps a stable preview
app-id and can install a managed Niri KDL block with backup/idempotent replace.
The same manager code is intended to be reused by future UI and watcher flows.

## Config Path Resolution

Shaula resolves config in this order:

1. `SHAULA_CONFIG_FILE`, when set and non-empty.
2. `$XDG_CONFIG_HOME/shaula/config.toml`, when `XDG_CONFIG_HOME` is set.
3. `$HOME/.config/shaula/config.toml`.

Missing config is not an error. A present but unreadable file maps to
`ERR_CONFIG_UNREADABLE`. A present file with invalid TOML subset, unknown keys,
or invalid values maps to `ERR_CONFIG_INVALID`.

## TOML Schema

Initial supported schema:

```toml
[capture]
region_capture_mode = "frozen"

[capture.after]
save_folder = "~/Pictures/shaula"

[capture.after.quick]
skip_preview = false
copy_to_clipboard = true
save_to_folder = false

[capture.after.area]
skip_preview = false
copy_to_clipboard = true
save_to_folder = false

[capture.after.fullscreen]
skip_preview = true
copy_to_clipboard = true
save_to_folder = true

[capture.after.all_screens]
skip_preview = true
copy_to_clipboard = true
save_to_folder = true

[preview.window]
mode = "floating"
focused = true
close_preview_on_save = true
width = 1100
height = 720
default_column_display = "normal"

[preview.window.floating_position]
x = 80
y = 80
relative_to = "top-left"
```

Defaults:

```toml
[capture]
region_capture_mode = "frozen"

[capture.after]
save_folder = "~/Pictures/shaula"

[capture.after.quick]
skip_preview = false
copy_to_clipboard = true
save_to_folder = false

[capture.after.area]
skip_preview = false
copy_to_clipboard = true
save_to_folder = false

[capture.after.fullscreen]
skip_preview = true
copy_to_clipboard = true
save_to_folder = true

[capture.after.all_screens]
skip_preview = true
copy_to_clipboard = true
save_to_folder = true

[preview.window]
mode = "floating"
focused = true
close_preview_on_save = true
width = 1100
height = 720
default_column_display = "normal"

[preview.window.floating_position]
relative_to = "top-left"
```

Supported `capture.region_capture_mode` values:

- `live`: the desktop keeps updating while selecting the region.
- `frozen`: selection happens against a still background for transient states.

Default after-capture behavior:

- quick and area show preview and copy on accept, without automatic folder save.
- fullscreen and all-screens skip preview, copy to clipboard, and save to the
  configured screenshot folder.

Supported `preview.window.mode` values:

- `auto`: let Niri decide.
- `tiling`: emit `open-floating false`.
- `floating`: emit `open-floating true`.
- `maximized`: emit `open-maximized true`.
- `maximized-to-edges`: emit `open-maximized-to-edges true`.
- `fullscreen`: emit `open-fullscreen true`.

Supported `default_column_display` values:

- `normal`
- `tabbed`

Supported `relative_to` values:

- `top-left`
- `top-right`
- `bottom-left`
- `bottom-right`
- `center`

`floating_position.x` and `floating_position.y` are optional. Niri
`default-floating-position` is emitted only when both are set and
`mode = "floating"`.

`close_preview_on_save` defaults to `true`. When set to `true`, a successful
preview Ctrl+S quick save sends the screenshot notification and then closes the
preview window. Failed saves keep the preview open.

Shaula sends screenshot thumbnails in desktop notifications using the
Freedesktop image-path hint. If your notification daemon does not show
thumbnails, check that image/icon display is enabled. On Mako, thumbnail size
depends on notification daemon settings such as max-icon-size.

## CLI

Inspect resolved config:

```bash
shaula config show --json
```

Create the default config file if missing:

```bash
shaula config init --json
```

Render the recommended Niri rule:

```bash
shaula config niri-window-rule --json
```

Extract only KDL:

```bash
shaula config niri-window-rule --json | jq -r '.result.kdl'
```

Install or replace Shaula's managed Niri block:

```bash
shaula config niri-install --json
```

`niri-install`:

- resolves `NIRI_CONFIG`, then `$XDG_CONFIG_HOME/niri/config.kdl`, then
  `$HOME/.config/niri/config.kdl`;
- creates a timestamped backup before modifying an existing file;
- only replaces content between Shaula's markers;
- is idempotent when the generated block is already installed;
- supports `--dry-run` and `--path <file>` for previews and tests.

## Stable Preview Identity

The preview window identity is stable:

```text
app_id = "dev.shaula.preview"
title = "Shaula Preview"
```

Generated Niri rules match this app-id:

```kdl
window-rule {
    match app-id="^dev\\.shaula\\.preview$"
    open-floating true
    open-focused true
    default-column-width { fixed 1100; }
    default-window-height { fixed 720; }
    default-column-display "normal"
}
```

Managed block installed by Shaula:

```kdl
// BEGIN SHAULA PREVIEW WINDOW RULE
window-rule {
    match app-id="^dev\\.shaula\\.preview$"
    open-floating true
    open-focused true
    default-column-width { fixed 1100; }
    default-window-height { fixed 720; }
    default-column-display "normal"
}
// END SHAULA PREVIEW WINDOW RULE
```

Reload Niri using the user's normal workflow after install.

## TOML Subset

The initial parser supports only Shaula's public config subset:

- `[preview.window]`
- `[preview.window.floating_position]`
- `[capture]`
- double-quoted strings
- booleans
- decimal integers
- comments with `#`
- blank lines

Unknown sections or keys fail with `ERR_CONFIG_INVALID` so typos do not get
silently ignored.
