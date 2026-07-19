# Shaula Context

Last updated: 2026-07-18

## Current release candidate

The current release candidate is **v0.1.6**. The production application,
helpers, tests, packaging, and release workflows are implemented in C and built
with Meson. The earlier v0.1.6 image-composition proposal is retained only as
historical design input in `docs/plan-v0.1.6.md`; image composition and
expandable-canvas work are not part of this release.

The release notes are in `docs/release-v0.1.6.md`. The release procedure and AUR
finalization steps are in `docs/releasing.md`.

## Runtime architecture

Shaula remains a short-lived CLI/helper application. It does not introduce a
resident Shaula daemon.

Capture routing follows ADR-0003:

- Niri and compatible wlroots sessions use the native `grim` route.
- Other supported Wayland sessions use the Screenshot portal route.
- Missing or unavailable routes return deterministic errors.
- The release and installer payload includes the CLI, overlay, Preview,
  Settings, portal screenshot helper, crop helper, clipboard provider, desktop
  entry, icons, Noctalia integration, and release manifest.

The `./dev` live commands export build-tree helper paths explicitly, so they
exercise the current checkout rather than relying on a previous local install.

## Clipboard publication and replacement

Clipboard publication uses `wl-copy` through the central process-execution
module. `wl-copy` owns the Wayland selection through data-control, avoiding the
input-event serial that prevents a standalone GTK/GDK CLI helper from reliably
claiming the clipboard on Niri and other wlroots compositors.

Deterministic clipboard outcomes are:

- provider exit code 35: clipboard unavailable;
- provider spawn failure: clipboard unavailable;
- malformed readiness: protocol invalid;
- readiness timeout: timeout;
- other provider exit: provider failed;
- text-copy failures: text-specific user-facing messages;
- readiness protocol stays off caller stdout and diagnostics remain on stderr.

Provider replacement implements ADR-0003 with a private session-bus
prepare/commit protocol and a unique clipboard MIME marker. For v0.1.6
providers, provider A remains alive until provider B:

1. owns the Wayland clipboard;
2. owns `dev.shaula.ClipboardProvider`;
3. emits readiness.

Only then does B commit A's exit. Failure or timeout by B lets A retain or
restore its valid selection. Clipboard replacement without the prepared marker
is external replacement and terminates the active provider.

A provider from before v0.1.6 has no handoff object. That one-time upgrade case
is detected explicitly and replaced through the legacy clipboard/name-loss
behavior, preventing an older provider from making all future clipboard copies
unavailable. All v0.1.6-to-v0.1.6 replacements use the ordered protocol.

Focused lifecycle and multi-process tests cover unavailable exit, spawn failure,
malformed readiness, timeout cleanup, no late clipboard claim, generic provider
failure, detached successful lifetime, stdout isolation, text error wording,
ordered replacement, failed replacement, and legacy-provider upgrade.

## Release artifacts and installer

Release CI builds, tests, stages, validates, checksums, and publishes:

- `shaula-linux-x86_64.tar.gz`
- `shaula-linux-aarch64.tar.gz`

AArch64 uses a QEMU-backed native `linux/arm64` Ubuntu build. The archive
validator checks every installed executable with `readelf`, verifies executable
modes, requires the exact manifest-backed payload, rejects symlinks and unlisted
files, and confirms that `share/icons/hicolor/index.theme` is absent.

The installer maps `x86_64` and `amd64` to the x86_64 archive, and `aarch64` and
`arm64` to the AArch64 archive. Installer contract tests exercise all four
machine strings, checksum verification, payload installation, and the missing
payload failure path.

Release CI rejects a tag unless it is exactly `v` plus the Meson project version
and `docs/release-<tag>.md` exists with the matching heading.

## AUR state

Tag release CI publishes both `aur/shaula` and `aur/shaula-bin` to the AUR as
`0.1.6-1`, with final immutable checksums and regenerated `.SRCINFO` files in
the temporary AUR clones.

- `shaula` builds the Meson/C source tree from tag `v0.1.6` on x86_64 and
  AArch64.
- `shaula-bin` selects the matching x86_64 or AArch64 release archive and
  installs the complete manifest-backed payload.
- `wl-clipboard` is a required runtime dependency for both packages.
- AUR publication uses the dedicated unencrypted deploy key
  `~/.ssh/id_aur` (comment `shaula-aur-deploy`), registered in the AUR
  account. It is independent from the personal `id_ed25519` key.

Manual AUR publish is replaced by the release pipeline (see
`docs/releasing.md`). Checked-in PKGBUILDs intentionally retain `SKIP`; the
workflow replaces those markers only in writable AUR clones before pushing.

## Verified release state

The current source state has passed:

- normal x86_64 build and all 31 Meson tests;
- strict x86_64 Werror build and all 31 tests;
- x86_64 ASan/UBSan build and all 31 tests;
- optimized x86_64 release build, all 31 tests, staged install, and archive
  validation;
- QEMU-backed native AArch64 Werror release build, all 31 tests, staged install,
  ELF architecture validation, and archive validation;
- combined x86_64/AArch64 `SHA256SUMS` verification and exact v0.1.6 release
  contract;
- local development install through `./dev dev-install --yes`;
- `git diff --check`;
- focused clipboard lifecycle and replacement tests;
- installer architecture-selection and AUR metadata consistency checks.
- CI regenerates AUR `.SRCINFO` files from writable temporary package copies;
  the repository remains mounted read-only inside the Arch validation container.
- Build and release containers install the required `wl-clipboard` runtime, and
  both workflows use the same writable-copy AUR metadata validation.
- GitHub workflows use Node.js 24-based v5 checkout and artifact actions.

Live Niri validation:

- `./dev all` completed a real 1920x1080 `grim-wlroots` all-screens capture with
  clipboard publication, no partial result, no warnings, and no stderr
  diagnostics.
- `./dev capture` launched the real area overlay. This non-interactive execution
  channel could not draw a region, so the test-owned overlay was cancelled;
  process termination and capture-lock cleanup were verified.

GNOME and KDE interactive portal/graphical validation is **not yet checked for
v0.1.6**. It is explicitly deferred to v0.1.7 and must not be reported as
passed.

## Release boundaries

No commit, merge, tag, push, GitHub release, AUR publication, issue, or pull
request has been created as part of this release-preparation work.
