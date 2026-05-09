# Shaula Noctalia Bar Widget

Minimal optional Noctalia Shell plugin for Shaula.

## Behavior

- Left click and right click open the Shaula menu.
- The menu exposes area, fullscreen/current-monitor, all-screens, Settings, doctor, screenshots-folder, and bug-report actions.
- Settings launches `shaula settings`; Noctalia does not read or write Shaula config.
- No screenshot logic runs in Noctalia; every action calls the Shaula CLI.

Noctalia's base context menu disappears almost immediately after selecting an
action, so the widget intentionally does not add capture delays before invoking
Shaula commands.

## Manual Enable

If the installer could not safely edit Noctalia config, copy this directory to:

```sh
~/.config/noctalia/plugins/shaula/
```

Then enable the `shaula` plugin in Noctalia's plugin settings and add `plugin:shaula` to a bar section if Noctalia does not add it automatically.
