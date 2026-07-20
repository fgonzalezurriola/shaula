# Shaula v0.1.7 (Draft)

v0.1.7 focuses on a compositor-independent Wayland launch and shortcut
experience, clearer Settings behavior, and predictable installation.

## Included

- The universal GTK menu is the normal graphical application entry point.
- Global shortcuts use only the XDG GlobalShortcuts portal. Shaula does not
  modify compositor keybindings.
- Settings keeps Global Shortcuts first, reports unavailable portal support
  prominently, opens the universal menu, and requests desktop-owned
  configuration for Quick, Area, Fullscreen, and All Screens.
- `./dev install` builds and installs the complete checkout without additional
  flags and reloads Noctalia when detected.
- The portable installer checks mandatory runtime support before changing user
  files and renders important terminal failures in red.
- AUR packages declare mandatory dependencies and their hooks warn prominently
  when no capture infrastructure is detected.

## Release Gates

- Normal, strict Werror, and ASan/UBSan test suites pass.
- Installer, release, archive, and AUR metadata contracts pass.
- Optimized x86_64 and AArch64 archives pass architecture and manifest checks.
- A local `./dev install` completes successfully.
- GNOME and KDE graphical sessions validate Screenshot and GlobalShortcuts
  portal flows, including shortcut configuration and one real activation.
- Niri validates the universal-menu fallback when GlobalShortcuts is
  unsupported and retains a working native capture route.

## Not Yet Claimed

GNOME, KDE, optimized archive, and AArch64 results must remain unclaimed until
their corresponding manual or artifact checks have actually completed.
