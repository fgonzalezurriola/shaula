# ADR 0003: Universal Wayland capture and clipboard ownership

## Status

Accepted.

## Context

Shaula's core experience requires capture, area selection, Preview, saving, and copying on a Wayland session without asking users to choose capture or clipboard tools. GTK clipboard ownership from a standalone CLI is not reliable because Wayland selection requests require an input-event serial; Niri correctly rejects that request even when GDK reports local success.

## Decision

### Clipboard module

`src/clipboard/` is the single clipboard seam for Capture, CLI commands, and Preview. Callers publish either PNG bytes or UTF-8 text and receive one deterministic status. They do not spawn desktop tools or manage ownership.

Each publish pipes the complete PNG or UTF-8 payload to `wl-copy --type <mime>` through the central process-execution module. `wl-copy` uses Wayland data-control, owns the selection after Shaula exits, and returns a nonzero status when publication fails. Callers retain deterministic Shaula status and `ERR_*` mapping without managing the external process directly.

### Capture backend decision

`src/capabilities/runtime.{c,h}` remains the single backend-decision seam. A backend is selected only after its prerequisite is verified:

- compatible Niri/wlroots sessions use the `grim-wlroots` adapter only when `grim` is present;
- otherwise a verified `org.freedesktop.portal.Screenshot` interface selects the `portal-screenshot` adapter;
- otherwise the runtime reports no usable capture route.

There is no distinct Niri-native backend label because the implementation is `grim`.

Native area and quick capture use Shaula's layer-shell overlay. Portal area and quick capture invoke the portal helper directly and use the desktop portal's interactive picker. Portal area capture never prepares a frozen source and never launches `shaula-overlay`.

Portal helper cancellation, timeout, unavailability, and malformed/failed outcomes remain distinct through the capture command and map to deterministic `ERR_*` results.

### Installation and user state

Meson, release archives, packages, and the public installer own immutable application files: executables, helpers, the canonical desktop file, application icons, Preview runtime icons, and distributed integration payloads.

`shaula setup` and `shaula settings` own mutable user state: the Shaula config,
explicit onboarding/shortcut choices, the optional shortcut-provider autostart
entry, and optional Niri/Noctalia integration. Setup changes are idempotent,
validated, backed up, atomic, and symmetrical for install/remove operations.
Global shortcut session ownership and the narrowly scoped resident exception are
defined separately by ADR-0004.

`./dev` only orchestrates build, staging, installation, setup, validation, and development reloads. The public installer does not install system packages or choose a portal backend.

## Consequences

`wl-clipboard` is a required runtime dependency. Clipboard contents remain available after CLI or Preview exits through `wl-copy`'s data-control owner.

Generic desktop portal sessions use desktop-provided selection UI rather than Shaula's overlay. Native `grim` sessions retain Shaula's overlay and previous-area behavior.

Capture capability output describes verified routes rather than optimistic fallbacks, and installation can fail before completion when no core capture route exists.
