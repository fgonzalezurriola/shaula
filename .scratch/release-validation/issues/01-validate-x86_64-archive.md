# Validate the optimized x86_64 archive

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/release-validation/spec.md`

## What to build

Produce the optimized x86_64 release archive through the real release path and
validate its architecture, manifest, executable modes, runtime payload, and
installer compatibility. Capture enough evidence to make the release gate
repeatable.

## Acceptance criteria

- [ ] The optimized x86_64 archive builds from the current checkout.
- [ ] Archive, manifest, architecture, installer, and release contract checks pass.
- [ ] The evidence and any environment limitations are recorded in release context.

## Blocked by

None - can start immediately.
