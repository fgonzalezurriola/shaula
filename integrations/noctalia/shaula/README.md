# Shaula Noctalia Bar Widget

Minimal optional Noctalia Shell plugin for Shaula.

## Behavior

- Left click runs `shaula capture area --json`.
- Right click opens a small menu with area, fullscreen, focused output, doctor, and screenshots-folder actions.
- No screenshot logic runs in Noctalia; every action calls the Shaula CLI.

Fullscreen and focused-output actions close the Noctalia context menu first and wait briefly before invoking Shaula so the menu is less likely to appear in the capture.

## Manual Enable

If the installer could not safely edit Noctalia config, copy this directory to:

```sh
~/.config/noctalia/plugins/shaula/
```

Then enable the `shaula` plugin in Noctalia's plugin settings and add `plugin:shaula` to a bar section if Noctalia does not add it automatically.
