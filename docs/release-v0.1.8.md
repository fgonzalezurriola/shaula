# Shaula v0.1.8

v0.1.8 is a focused Niri setup UX patch for the v0.1.7 shortcut release.

## Included

- `shaula setup` reloads the live Niri config after it writes or removes managed
  Niri keybindings or the Preview window rule.
- The reload path validates the target Niri config before applying it with
  `niri msg action load-config-file`.
- Niri reload failures are advisory: setup still succeeds after writing valid
  managed config and prints the manual reload command.

## Validation

- Normal Meson test suite passes.
- Installer, release, archive, and AUR metadata contracts pass.
- `git diff --check` passes.
- Local development installation with `./dev dev-install --yes` completes.
- A temporary Niri harness confirmed setup runs `niri validate -c <config>` and
  `niri msg action load-config-file` after a managed config write.

## Deferred To v0.1.9

GNOME and KDE graphical portal validation remains unclaimed until those sessions
have tested capture, shortcut approval, configuration, activation, and
persistence. Optimized archive and AArch64 artifact validation also remains
required for v0.1.9. Configurable capture shortcuts from the Settings view are
also deferred to v0.1.9.
