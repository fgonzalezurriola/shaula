# Extract the portable service and migrate Noctalia

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/quickshell-integration/spec.md`

## What to build

Introduce the approved portable QuickShell service boundary and route the
existing Noctalia widget through it while preserving every current action,
installer behavior, diagnosis, and user-visible interaction.

## Acceptance criteria

- [ ] The portable service exposes only the architecture approved by ticket 01.
- [ ] Noctalia retains all current capture and folder/settings actions without user migration.
- [ ] Setup, uninstall, package payload, and doctor cover the shared service assets.
- [ ] Existing Noctalia behavior is manually validated after installation.

## Blocked by

- `.scratch/quickshell-integration/issues/01-approve-architecture-and-value.md`
