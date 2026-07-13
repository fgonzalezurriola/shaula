# Wayland Runtime Test Plan

This plan describes how to verify Shaula's Wayland runtime behavior across Niri,
wlroots, and generic Wayland portal sessions without turning the developer
machine into a fragile compositor lab.

## Recommendation

Use a hybrid setup:

1. Test Niri and overlay behavior on the real host session.
2. Test generic portal behavior in a VM or spare machine.
3. Test wlroots `grim` behavior in either a nested/manual session or a small VM.
4. Keep heavy VM images, package caches, and temporary captures off `/`.

Do not start by installing full GNOME/KDE/Sway/Hyprland stacks directly on the
main system unless there is already enough disk space and a rollback plan. The
host is still the best place to test Niri timing, layer-shell overlay behavior,
clipboard integration, notification behavior, and real user config paths. VMs
are better for broad compositor coverage and portal UX variance.

## Disk-Space Guardrails

Before any heavy manual test setup:

```bash
df -h / /home
du -sh ~/.cache 2>/dev/null || true
```

Prefer a large non-root path for temporary work:

```bash
export SHAULA_LAB_ROOT=$HOME/dev/shaula-lab
mkdir -p "$SHAULA_LAB_ROOT"/{tmp,vm-images,captures}
export TMPDIR="$SHAULA_LAB_ROOT/tmp"
export SHAULA_OUTPUT_DIR="$SHAULA_LAB_ROOT/captures"
```

For VM work, create the libvirt/virt-manager storage pool under
`$SHAULA_LAB_ROOT/vm-images` or another large mount. Avoid the default root-backed
pool if `/` is tight.

## Test Lanes

### Lane 0: Contract Checks Without Real Compositor Changes

Purpose: verify deterministic JSON, backend selection, portal fallback warnings,
and unsupported-compositor behavior.

Run after every implementation change:

```bash
./dev check
git diff --check
```

Useful targeted checks:

```bash
env SHAULA_COMPOSITOR=wayland WAYLAND_DISPLAY=wayland-1 \
  SHAULA_PORTAL_AVAILABLE=1 SHAULA_PORTAL_WINDOW_CAPABLE=1 \
  ./dev run capabilities list --json

env SHAULA_COMPOSITOR=wayland WAYLAND_DISPLAY=wayland-1 \
  SHAULA_PORTAL_AVAILABLE=1 \
  ./dev run preflight --json

env SHAULA_COMPOSITOR=wayland WAYLAND_DISPLAY=wayland-1 \
  SHAULA_PORTAL_AVAILABLE=0 \
  ./dev run capabilities list --json
```

Expected results:

- Portal-available generic Wayland reports backend `portal-screenshot`.
- Portal fallback emits `portal_fallback`.
- Window remains disabled, even when `portal_window_capable=true`.
- Generic Wayland without portal returns `ERR_UNSUPPORTED_COMPOSITOR`.

### Lane 1: Host Niri Runtime

Purpose: verify the real user-facing path that matters most: Niri direct capture,
layer-shell overlay selection, previous-area, clipboard, notifications, and
preview.

Use the local dev build:

```bash
./dev dev-install --yes
./dev run doctor --json
./dev run capabilities list --json
./dev run preflight --json
```

Manual interactive checks:

```bash
./dev capture
./dev all
```

Expected results:

- Backend is `niri-wayland-direct` on Niri.
- Overlay opens for area/quick capture.
- `previous-area` works after a successful area capture.
- No portal warning is emitted on the normal Niri direct path.
- Preview Copy, Save, Save As, and Done still behave normally.

Use this lane for timing-sensitive issues. VM results are not trustworthy for
overlay latency, layer-shell focus, real monitor scale, notification daemons, or
clipboard ergonomics.

### Lane 2: Host Forced Portal Smoke

Purpose: verify that the installed portal helper can execute from the local build
without installing another desktop stack.

Only run this manually in an active graphical session where a portal backend is
installed:

```bash
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capabilities list --json
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capture fullscreen --json --no-preview
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capture area --json --no-preview
```

Expected results:

- Backend is `portal-screenshot`.
- Successful captures emit `capture_backend_degraded`.
- Area capture uses portal selection and emits `capture_selection_portal`.
- Cancelling the portal picker maps to `ERR_SELECTION_CANCELLED`.
- Portal timeout maps to `ERR_IPC_TIMEOUT`.

This lane may show compositor/desktop-specific portal prompts. That is useful
evidence, but it is not a replacement for GNOME/KDE VM coverage.

### Lane 3: Generic Wayland Portal VM

Purpose: verify GNOME/KDE-style portal compatibility without installing full DEs
on the host.

Suggested guests:

- Fedora Workstation for GNOME portal behavior.
- Fedora KDE Spin or another KDE Wayland image for KDE portal behavior.

Manual setup notes:

- Place VM disks under a large storage path, not `/var/lib/libvirt/images` if `/`
  is tight.
- Enable clipboard/file sharing only if needed to move artifacts.
- Build Shaula in the VM or copy a local release/dev archive into it.
- Install only runtime dependencies required for the guest lane.

Inside each VM:

```bash
shaula doctor --json
shaula preflight --json
shaula capabilities list --json
shaula capture fullscreen --json --no-preview
shaula capture area --json --no-preview
shaula capture previous-area --json --no-preview
```

Expected results:

- Generic Wayland with portal reports backend `portal-screenshot`.
- Overlay does not appear for portal area selection.
- `previous-area` returns `ERR_CAPTURE_MODE_UNSUPPORTED`.
- Portal cancellation returns `ERR_SELECTION_CANCELLED`.
- Captures are degraded but successful when the portal allows them.

Record guest name, desktop, portal package/backend, and any prompt behavior in
the evidence notes.

### Lane 4: wlroots `grim` VM Or Nested Session

Purpose: verify the wlroots fast path where `grim` is available.

Suggested targets:

- Sway first, because it is small and predictable.
- Hyprland later, only if there is a specific compatibility concern.

Inside the target session:

```bash
shaula doctor --json
shaula capabilities list --json
shaula capture fullscreen --json --no-preview
shaula capture all-screens --json --no-preview
shaula capture area --json --no-preview
```

Expected results:

- wlroots with `grim` reports backend `grim-wlroots`.
- Overlay is supported for area selection.
- If `grim` is missing but portal is available, runtime falls back to
  `portal-screenshot`.
- If both `grim` and portal are unavailable, capture fails deterministically
  rather than pretending success.

## Evidence Captured 2026-05-31

Lab root: `/home/fgonz/dev/shaula-lab` on `/home`, not `/`. Total lab size after
two guests was about 5.5 GiB: 1.4 GiB images, 4.1 GiB VM overlays, 84 KiB
captured artifacts.

Images used:

- Fedora 44 Cloud Base Generic x86_64 from the official Fedora release tree.
  SHA256 verified:
  `28680fe5b371a5a82ebf43a31926e086a168e59949d03969c5093e7071f90b7f`.
- Ubuntu 26.04 LTS Cloud amd64 from the official Ubuntu Cloud release tree.
  `SHA256SUMS` verified for `ubuntu-26.04-server-cloudimg-amd64.img`.

Common method:

- Created direct QEMU/KVM guests with qcow2 overlays under the lab root.
- Copied the current working tree, including unstaged Wayland runtime changes,
  into `/home/shaula/src` inside each guest.
- Copied the local Zig 0.16.0 toolchain into `/home/shaula/zig-0.16.0`.
- Installed distro packages for GTK 4, gtk4-layer-shell, wlroots/Sway,
  `grim`, `slurp`, `wl-clipboard`, and xdg-desktop-portal-wlr.
- Ran `zig build`, contract smoke checks, `./dev check` inside a real Sway
  headless session, and real `grim-wlroots` capture commands.

| Guest | Kernel | Package evidence | Result |
| --- | --- | --- | --- |
| Fedora Linux 44 Cloud | `6.19.10-300.fc44.x86_64` | `gtk4-layer-shell-devel-1.3.0-1.fc44`, `xdg-desktop-portal-wlr-0.8.2-1.fc44`, `sway-1.11-3.fc44`, `grim-1.5.0-3.fc44`, `slurp-1.5.0-6.fc44`, `wl-clipboard-2.2.1^git20251124.e808203-2.fc44` | `zig build` passed; portal contract smoke passed; generic Wayland without portal returned `ERR_UNSUPPORTED_COMPOSITOR`; `./dev check` passed inside Sway headless; `capture fullscreen` and `capture all-screens` produced 1280x720 PNGs with backend `grim-wlroots`. |
| Ubuntu 26.04 LTS Cloud | `7.0.0-15-generic` | `libgtk4-layer-shell-dev 1.3.0-1`, `xdg-desktop-portal-wlr 0.8.1-1`, `sway 1.11-3`, `grim 1.4.0+ds-2build3`, `slurp 1.6.0-1`, `wl-clipboard 2.2.1-2build1` | `zig build` passed; portal contract smoke passed; generic Wayland without portal returned `ERR_UNSUPPORTED_COMPOSITOR`; `./dev check` passed inside Sway headless; `capture fullscreen` and `capture all-screens` produced 1280x720 PNGs with backend `grim-wlroots`. |
| Ubuntu 26.04 LTS Cloud, GNOME headless | `7.0.0-15-generic` | `gnome-shell 50.1-0ubuntu1`, `xdg-desktop-portal 1.21.1+ds-1ubuntu3`, `xdg-desktop-portal-gnome 50.0-0ubuntu1` | `gnome-shell --wayland --headless --virtual-monitor 1280x720` started a real Wayland socket. The portal Screenshot property returned version `2`; `capabilities list` and `preflight` passed with compositor `GNOME`, backend `portal-screenshot`, `portal_available=true`, `overlay_supported=false`, warnings `window_capture_degraded` and `portal_fallback`. Unattended `capture fullscreen` timed out after 12 seconds because the portal flow needs an interactive prompt/window association in this headless lane. |
| Ubuntu 26.04 LTS Cloud, KDE/KWin virtual | `7.0.0-15-generic` | `kwin-wayland 4:6.6.4-0ubuntu1`, `plasma-workspace 4:6.6.4-0ubuntu2`, `xdg-desktop-portal-kde 6.6.4-0ubuntu1` | `kwin_wayland --virtual --width 1280 --height 720` started, but `xdg-desktop-portal-kde` in this headless/virtual session did not expose `org.freedesktop.portal.Screenshot`; `gdbus` returned `No such interface`, and Shaula returned `ERR_UNSUPPORTED_COMPOSITOR` for `capabilities list`/`preflight`. A forced capture was blocked by `ERR_CAPTURE_MODE_UNSUPPORTED`, confirming the capability guard does not claim KDE portal support when the Screenshot interface is absent. |

Important runner note: after installing `grim`, running `./dev check` from a
plain SSH session fails the optional real-backend test because the process has
no active Wayland compositor. Run VM checks inside the Sway headless wrapper, or
run contract-only checks before installing `grim`, if the goal is a non-graphical
package/build smoke.

Artifacts kept outside the repository:

- `/home/fgonz/dev/shaula-lab/artifacts/fedora44/`
- `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/`
- `/home/fgonz/dev/shaula-lab/artifacts/run-dev-check-in-sway.sh`
- GNOME/KDE runners and outputs under
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/run-gnome-portal-test.sh`,
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/gnome-portal-test.out`,
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/run-kde-portal-test.sh`, and
  `/home/fgonz/dev/shaula-lab/artifacts/ubuntu2604/kde-portal-test.out`.

Remaining gaps:

- GNOME portal capture success still needs an interactive graphical guest or
  spare hardware to accept the portal prompt.
- KDE portal Screenshot support still needs a full graphical Plasma session or
  spare hardware; the headless KWin virtual lane did not expose the public
  Screenshot portal interface.
- Host Niri overlay UX still needs the real host lane: `./dev capture` and
  `./dev all`.

## Evidence Template

Use this small record for every manual lane:

```markdown
## YYYY-MM-DD compositor evidence

- Host/guest:
- Desktop/compositor:
- Session type:
- Portal packages/backend:
- Shaula build:
- Commands run:
- Expected:
- Actual:
- Artifacts:
- Notes:
```

## When To Avoid A VM

Avoid VM evidence for:

- overlay first-paint latency,
- Niri monitor/focused-output behavior,
- hotplug or fractional-scale confidence,
- real clipboard behavior,
- notification UX,
- Noctalia integration.

Those belong on the real host or a spare physical Wayland machine.

## When To Avoid Host Installs

Avoid host installs for:

- full GNOME/KDE desktop stacks just to test portal behavior,
- experimental wlroots compositors that pull large dependency sets,
- tests that require changing login/session defaults,
- anything that would consume root filesystem space.

Use VMs or a spare machine for those.

## Minimal Release Gate

Before claiming broad Wayland support is ready:

- Lane 0 passes.
- Lane 1 passes on the host Niri session.
- Lane 2 passes or has a documented portal-specific blocker.
- Lane 3 passes on at least one GNOME or KDE portal VM.
- Lane 4 passes on Sway or has a documented `grim`/portal fallback result.
- `CONTEXT.md` is updated with the latest known compositor evidence.
