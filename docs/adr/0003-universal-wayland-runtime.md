# ADR 0003: Universal Wayland capture and clipboard ownership

## Status

Accepted.

## Context

Shaula's core experience requires capture, area selection, Preview, saving, and copying on a Wayland session without asking users to choose capture or clipboard tools. The previous implementation exposed compositor-specific assumptions through capability labels, prepared portal area captures with `grim` and the Shaula overlay, and delegated clipboard lifetime to `wl-copy`.

## Decision

### Clipboard module

`src/clipboard/` is the single clipboard seam for Capture, CLI commands, and Preview. Callers publish either PNG bytes or UTF-8 text and receive one deterministic status. They do not spawn desktop tools or manage ownership.

Each successful publish starts the bundled `shaula-clipboard-provider` helper. The initiating process sends a versioned, length-delimited payload over stdin and waits for one readiness line on the helper's private stdout pipe. The helper loads the entire payload before claiming the Wayland clipboard, so clipboard validity does not depend on the source file or initiating process.

The provider process owns the clipboard until another client claims it, the Wayland display closes, or the helper receives a termination signal. Replacement uses a private prepare/commit exchange addressed to the previous provider's unique session-bus name and a unique clipboard MIME marker. Provider B prepares provider A, claims the Wayland clipboard with that marker, replaces `dev.shaula.ClipboardProvider`, emits readiness, and only then commits A's exit. A failed or timed-out B leaves A alive and allows it to retain or restore its payload. A clipboard change without the prepared marker is external replacement and still terminates the active provider. A provider from before v0.1.6 does not expose the handoff object; that one-time upgrade case is detected explicitly and replaced through the legacy clipboard/name-loss behavior instead of blocking every future copy. All v0.1.6-to-v0.1.6 replacements use the ordered protocol. This avoids PID files, stale locks, stale providers, upgrade deadlocks, and kill-before-ready races.

Helper diagnostics are stderr-only. Stdout is reserved for the readiness protocol, so nested clipboard work cannot corrupt Shaula's JSON output.

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

`shaula setup` owns mutable user state: the Shaula config and optional Niri/Noctalia integration. Setup changes are idempotent, validated, backed up, atomic, and symmetrical for install/remove operations.

`./dev` only orchestrates build, staging, installation, setup, validation, and development reloads. The public installer does not install system packages or choose a portal backend.

## Consequences

The installed payload includes one additional GTK helper. Clipboard contents remain available after CLI or Preview exits, at the cost of one small provider process while Shaula owns the current selection.

Generic desktop portal sessions use desktop-provided selection UI rather than Shaula's overlay. Native `grim` sessions retain Shaula's overlay and previous-area behavior.

Capture capability output describes verified routes rather than optimistic fallbacks, and installation can fail before completion when no core capture route exists.
