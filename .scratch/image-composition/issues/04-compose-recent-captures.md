# Compose recent Shaula captures

Type: AFK
Status: resolved

## Parent

`.scratch/image-composition/spec.md`

## What to build

Expose a bounded set of captures created by Shaula as a composition source,
with explicit lifetime and cleanup ownership. A selected recent capture must
resolve into an immutable image asset and remain valid after insertion without
creating global clipboard history.

## Acceptance criteria

- [x] The composition sheet lists only the approved bounded recent-capture set.
- [x] Missing, expired, or deleted entries fail clearly without document mutation.
- [x] Inserted assets survive source cleanup through the established ownership contract.
- [x] Retention count, byte budget, and cleanup behavior remain within approved limits.

## Blocked by

- `.scratch/image-composition/issues/02-compose-files-vertically.md`

## Answer

Implemented in the runtime recent-capture cache. Successful Shaula captures
are copied into `$XDG_CACHE_HOME/shaula/recent-captures`; startup/listing and
registration prune oldest entries to 12 items and 256 MiB. Preview decodes a
selected entry into a ref-counted Image asset, so deleting or pruning its source
does not affect an inserted annotation.
