# ADR 0004: Portal-first global shortcuts with a narrow resident provider

- Status: Accepted
- Date: 2026-07-19

## Context

Shaula's capture, Preview, Settings, and CLI commands are intentionally short-lived. The XDG Desktop Portal GlobalShortcuts API is session-based, however: activations are delivered through `Activated` and `Deactivated` signals to the live process that owns the shortcut session. Merely finding the D-Bus interface does not prove that a desktop can create a session, bind shortcuts, return approved mappings, or deliver activations.

Users also need one graphical and terminal-facing shortcut choice rather than compositor-specific setup knowledge. Niri currently lacks a viable GlobalShortcuts portal in common deployments, while Shaula already owns a narrowly scoped, backup-backed Niri configuration mechanism. The universal graphical menu remains the final fallback on every desktop.

The official API contract is the XDG Desktop Portal GlobalShortcuts documentation: <https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html>.

## Decision

`src/shortcuts/` is the single shortcut subsystem. Settings, `shaula setup`, diagnostics, and explicit `shaula shortcuts` commands use its generic enable, disable, query, and repair operations. Callers do not select a compositor adapter.

Backend selection is portal-first:

1. The portal adapter starts `shaula-shortcut-provider` only after explicit user enablement.
2. The provider reads the portal version, completes `CreateSession`, subscribes to activation, deactivation, shortcut-change, and session-close signals, calls `BindShortcuts`, and verifies the approved mappings with `ListShortcuts`.
3. The backend is reported active only when all four logical actions are returned and activation delivery is subscribed. Returned `trigger_description` values are authoritative; preferred triggers are requests, not claims.
4. Desktop cancellation or rejection is preserved as permission-denied state.
5. Technical portal non-viability removes provider autostart state and probes for a real Niri configuration.
6. When Niri is detected, Shaula checks `Ctrl+Shift+1` through `4` for conflicts, backs up the complete config, and installs only its marked managed block.
7. Without a viable portal or Niri configuration, Shaula reports unsupported and keeps the universal menu and desktop launcher actions available.

The four logical actions are quick capture, area capture, fullscreen capture, and all-screens capture. Their requested preferred triggers are `Ctrl+Shift+1` through `Ctrl+Shift+4`.

## Resident provider responsibilities

`shaula-shortcut-provider` is the only resident Shaula process and is deliberately narrow. It:

- owns one `org.freedesktop.portal.GlobalShortcuts` session;
- exposes a small private D-Bus control surface for status, reconfiguration, and clean shutdown;
- maps approved activation IDs to existing Shaula capture argv arrays;
- launches commands directly with `GSubprocess`, never through a shell;
- suppresses duplicate provider instances with the well-known per-user D-Bus name `dev.shaula.ShortcutProvider`;
- suppresses repeated or concurrent capture dispatch through a deterministic activation gate;
- records deterministic provider state for Settings and CLI status;
- closes its session on disable and reconnects with bounded backoff after portal restart or session loss.

It does not capture pixels, implement capture modes, own Preview state, or become a general Shaula daemon. Niri configuration is handled separately by the non-resident managed-config adapter. The normal `shaula capture ...` commands therefore remain short-lived and retain all existing capture locks and error behavior.

## Lifecycle mechanism

Shaula uses a generated per-user XDG autostart desktop entry at `$XDG_CONFIG_HOME/autostart/dev.shaula.ShortcutProvider.desktop`.

This mechanism is portable across desktops that implement the freedesktop autostart convention, requires no privileged service manager integration, and keeps mutable state under user control. `shaula setup` and `shaula settings` create the entry only after explicit portal-shortcut enablement. Disable and uninstall ask the live provider to stop, remove the autostart entry, and remove provider status state. Package hooks never mutate a user's home directory.

The provider executable itself is immutable package payload installed by Meson, included in the release manifest and both AUR package paths, and removed with the rest of the installed payload.

## Failure and status contracts

The generic status model distinguishes disabled, active, permission pending, permission denied, Niri conflict, unsupported, provider unavailable, reconnecting/session lost, and invalid configuration. Stable failures include `ERR_NIRI_KEYBIND_CONFLICT` in addition to the portal and configuration taxonomy.

Enable, disable, repair, and repeated operations are idempotent. Explicit decline is persisted separately from installed state.

## Consequences

Shaula gains one narrowly scoped resident process only for approved portal shortcuts. Niri fallback is file-based, backup-backed, marked, idempotent, and removed symmetrically. Graphical capture and desktop actions remain independent of both shortcut backends.

Portal support is claimed only where the complete session, binding, listing, and activation contract works. Automated seams validate protocol state and dispatch behavior. GNOME, KDE Plasma, and other portal implementations still require live approval and activation checks on their respective desktops.
