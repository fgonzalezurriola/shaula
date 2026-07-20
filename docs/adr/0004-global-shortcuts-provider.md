# ADR 0004: Portal-first global shortcuts with a narrow resident provider

- Status: Accepted
- Date: 2026-07-19

## Context

Shaula's capture, Preview, Settings, and CLI commands are intentionally short-lived. The XDG Desktop Portal GlobalShortcuts API is session-based, however: activations are delivered through `Activated` and `Deactivated` signals to the live process that owns the shortcut session. Merely finding the D-Bus interface does not prove that a desktop can create a session, bind shortcuts, return approved mappings, or deliver activations.

Users also need one graphical and terminal-facing shortcut choice rather than compositor-specific setup knowledge. Niri's managed keybinding implementation already provides conflict detection, managed markers, backups, atomic replacement, and symmetrical removal, so it remains valuable as a fallback when the portal path is technically unavailable.

The official API contract is the XDG Desktop Portal GlobalShortcuts documentation: <https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html>.

## Decision

`src/shortcuts/` is the single shortcut subsystem. Settings, `shaula setup`, diagnostics, and explicit `shaula shortcuts` commands use its generic enable, disable, query, and repair operations. Callers do not select a compositor adapter.

Backend selection is portal-first:

1. The portal adapter starts `shaula-shortcut-provider` only after explicit user enablement.
2. The provider reads the portal version, completes `CreateSession`, subscribes to activation, deactivation, shortcut-change, and session-close signals, calls `BindShortcuts`, and verifies the approved mappings with `ListShortcuts`.
3. The backend is reported active only when all four logical actions are returned and activation delivery is subscribed. Returned `trigger_description` values are authoritative; preferred triggers are requests, not claims.
4. Desktop cancellation or rejection is preserved as permission-denied state. It does not silently install Niri bindings.
5. Technical portal non-viability—unsupported API, unavailable provider, invalid session behavior, or unusable configuration—allows the manager to try the managed Niri adapter.
6. When neither adapter is viable, setup succeeds with an unsupported shortcut status. Desktop launcher actions remain available.

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

It does not capture pixels, implement capture modes, own Preview state, edit compositor configuration, or become a general Shaula daemon. The normal `shaula capture ...` commands therefore remain short-lived and retain all existing capture locks and error behavior.

## Lifecycle mechanism

Shaula uses a generated per-user XDG autostart desktop entry at `$XDG_CONFIG_HOME/autostart/dev.shaula.ShortcutProvider.desktop`.

This mechanism is portable across desktops that implement the freedesktop autostart convention, requires no privileged service manager integration, and keeps mutable state under user control. `shaula setup` and `shaula settings` create the entry only after explicit portal-shortcut enablement. Disable and uninstall ask the live provider to stop, remove the autostart entry, and remove provider status state. Package hooks never mutate a user's home directory.

The provider executable itself is immutable package payload installed by Meson, included in the release manifest and both AUR package paths, and removed with the rest of the installed payload.

## Failure and status contracts

The generic status model distinguishes disabled, active, permission pending, permission denied, conflict, unsupported, provider unavailable, reconnecting/session lost, and invalid configuration. Stable failures map to `ERR_SHORTCUTS_UNSUPPORTED`, `ERR_SHORTCUT_PERMISSION_DENIED`, `ERR_SHORTCUT_PROVIDER_UNAVAILABLE`, `ERR_SHORTCUT_SESSION_LOST`, and `ERR_SHORTCUT_CONFIGURATION_INVALID`; existing Niri conflicts continue to use `ERR_NIRI_KEYBIND_CONFLICT`.

Enable, disable, repair, and repeated operations are idempotent. Niri removal remains symmetrical and preserves its existing backup and managed-block rules. Explicit decline is persisted separately from installed state so first-run behavior does not repeatedly prompt after a user choice.

## Consequences

Shaula gains one narrowly scoped resident process only for approved portal shortcuts. Graphical capture and desktop actions remain independent of the provider, so provider or portal failure does not prevent ordinary use.

Portal support is claimed only where the complete session, binding, listing, and activation contract works. Automated seams validate protocol state and dispatch behavior, while live Niri validation covers the managed fallback. GNOME and KDE interactive approval and activation remain unverified until tested on those desktops.
