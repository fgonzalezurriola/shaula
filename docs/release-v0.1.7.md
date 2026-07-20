# Shaula v0.1.7

v0.1.7 focuses on a compositor-independent Wayland launch and shortcut
experience, clearer Settings behavior, and predictable installation.

## Included

- The universal GTK menu is the normal graphical application entry point.
- Global shortcuts prefer XDG GlobalShortcuts. When the portal is not viable and
  Niri is detected, Shaula backs up the Niri config and installs a marked,
  removable keybinding block.
- Settings keeps Global Shortcuts first, reports unavailable portal support
  prominently, opens the universal menu, and requests desktop-owned
  configuration for Quick, Area, Fullscreen, and All Screens.
- `./dev install` builds and installs the complete checkout without additional
  flags and reloads Noctalia when detected.
- The portable installer checks mandatory runtime support before changing user
  files and renders important terminal failures in red.
- AUR packages declare mandatory dependencies and their hooks warn prominently
  when no capture infrastructure is detected.

## Validation

- Normal, strict Werror, and ASan/UBSan test suites pass.
- Installer, release, archive, and AUR metadata contracts pass.
- A local `./dev install` completes successfully.
- Niri validates managed keybinding installation, backup, activation, symmetric
  removal, and the universal-menu fallback.

## Deferred To v0.1.8

GNOME and KDE graphical portal validation remains unclaimed until those sessions
have tested capture, shortcut approval, configuration, activation, and
persistence. Optimized archive and AArch64 artifact validation also remains
required for v0.1.8. Configurable capture shortcuts from the Settings view are
also deferred to v0.1.8.
