# Validate GNOME portal journeys

Type: HITL
Status: ready-for-human

## Parent

`.scratch/release-validation/spec.md`

## What to build

Run the complete supported capture and GlobalShortcuts journeys in a real GNOME
Wayland session, including desktop approval, configuration, activation,
preference rebinding, persistence, and actionable failure behavior.

## Acceptance criteria

- [ ] Capture modes complete through the Screenshot portal and open the expected result.
- [ ] Shortcut approval, activation, rebinding, and persistence work across restart.
- [ ] Results, desktop versions, and any failures are recorded as release evidence.

## Blocked by

None - can start immediately.
