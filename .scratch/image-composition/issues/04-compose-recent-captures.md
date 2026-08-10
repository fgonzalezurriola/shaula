# Compose recent Shaula captures

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/image-composition/spec.md`

## What to build

Expose a bounded set of captures created by Shaula as a composition source,
with explicit lifetime and cleanup ownership. A selected recent capture must
resolve into an immutable image asset and remain valid after insertion without
creating global clipboard history.

## Acceptance criteria

- [ ] The composition sheet lists only the approved bounded recent-capture set.
- [ ] Missing, expired, or deleted entries fail clearly without document mutation.
- [ ] Inserted assets survive source cleanup through the established ownership contract.
- [ ] Retention count, byte budget, and cleanup behavior remain within approved limits.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`
