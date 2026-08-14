# Validate the optimized x86_64 archive

Type: AFK
Status: resolved

## Parent

`.scratch/release-validation/spec.md`

## What to build

Produce the optimized x86_64 release archive through the real release path and
validate its architecture, manifest, executable modes, runtime payload, and
installer compatibility. Capture enough evidence to make the release gate
repeatable.

## Acceptance criteria

- [x] The optimized x86_64 archive builds from the current checkout.
- [x] Archive, manifest, architecture, installer, and release contract checks pass.
- [x] The evidence and any environment limitations are recorded in release context.

## Blocked by

None - can start immediately.

## Answer

The current checkout was built through `./dev release` with release build type,
stripping, and LTO. Meson installed the payload and produced
`dist/validation-x86_64/shaula-linux-x86_64.tar.gz` (443956 bytes,
SHA-256 `ed2f3ed333a744537da871a812c6d454514d150082ed717fb9a74d1cfb3a4b36`).
`assert-release-archive.sh` passed the manifest, executable modes, ELF
x86_64 architecture, payload exactness, and symlink/index.theme guards.
`assert-install-contract.sh "$PWD" build-release` also passed checksum,
installer selection, payload installation, and route/setup checks.
