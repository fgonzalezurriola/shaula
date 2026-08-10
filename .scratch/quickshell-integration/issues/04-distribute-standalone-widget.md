# Distribute the standalone QuickShell widget

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/quickshell-integration/spec.md`

## What to build

Provide an installable standalone widget for an approved generic QuickShell
configuration, including setup, uninstall, packaging, diagnosis, documentation,
and a minimal working example using the shared service.

## Acceptance criteria

- [ ] A supported generic QuickShell config can install and invoke all exposed Shaula actions.
- [ ] Setup, uninstall, release payload, and doctor handle standalone assets symmetrically.
- [ ] The example demonstrates capture lifecycle and panel coordination end to end.
- [ ] Existing Noctalia installations remain compatible through the documented migration window.

## Blocked by

- `.scratch/quickshell-integration/issues/03-add-ipc-handshake-and-capture-state.md`
