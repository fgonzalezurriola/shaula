# Validate the AArch64 archive

Type: AFK
Status: ready-for-agent

## Parent

`.scratch/release-validation/spec.md`

## What to build

Produce the native or QEMU-backed AArch64 release archive through the real
release path and validate the updated executable payload, architecture,
manifest, and installer selection contract.

## Acceptance criteria

- [ ] The AArch64 archive builds in the supported native or QEMU-backed environment.
- [ ] Every executable and manifest entry validates as AArch64 release payload.
- [ ] Installer selection and release contracts pass and evidence is recorded.

## Blocked by

None - can start immediately.
