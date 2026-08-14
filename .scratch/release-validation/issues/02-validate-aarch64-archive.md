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

## Comments

The current environment has no configured `SHAULA_MESON_CROSS_FILE`, AArch64
compiler/sysroot, QEMU user emulator, or running Docker daemon. `./dev
release-arm` therefore cannot build a current-checkout artifact here. The
previous checked-out artifact at `dist/aarch64/shaula-linux-aarch64.tar.gz`
passes `assert-release-archive.sh`, but it predates this checkout and is not
claimed as current release evidence. This ticket remains ready for an
ARM-capable/QEMU release runner.
