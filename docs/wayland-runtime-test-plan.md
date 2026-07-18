# Wayland Runtime Test Plan

This plan verifies Shaula's capture, Preview, save, and clipboard contracts across
Niri, other wlroots compositors, and generic portal-based Wayland desktops.

## Test strategy

Use four layers:

1. Deterministic unit and contract checks on every change.
2. Real Niri checks on the developer's active session.
3. Generic portal checks in a GNOME or KDE graphical guest or spare machine.
4. Native `grim-wlroots` checks in Sway or another compatible compositor.

Do not install additional desktop stacks or portal implementations on the main
machine solely for testing. The desktop owns portal backend selection.

## Disk guardrails

Before creating guests or large capture sets:

```bash
df -h / /home
export SHAULA_LAB_ROOT=$HOME/dev/shaula-lab
mkdir -p "$SHAULA_LAB_ROOT"/{tmp,vm-images,captures}
export TMPDIR="$SHAULA_LAB_ROOT/tmp"
```

Keep VM images and captures off a constrained root filesystem.

## Lane 0: deterministic contracts

Run after every code change:

```bash
./dev check
git diff --check
```

Targeted capability checks:

```bash
env SHAULA_COMPOSITOR=wayland WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=0 SHAULA_PORTAL_AVAILABLE=1 \
  ./dev run capabilities list --json

env SHAULA_COMPOSITOR=niri WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=1 SHAULA_PORTAL_AVAILABLE=0 \
  ./dev run capabilities list --json

env SHAULA_COMPOSITOR=niri WAYLAND_DISPLAY=wayland-1 \
  SHAULA_GRIM_AVAILABLE=0 SHAULA_PORTAL_AVAILABLE=0 \
  ./dev run capabilities list --json
```

Expected:

- Generic Wayland with a verified Screenshot portal reports
  `portal-screenshot`.
- Niri with `grim` reports `grim-wlroots`.
- Niri without `grim` probes and selects a verified portal when available.
- Niri without either route returns `ERR_CAPTURE_BACKEND_UNAVAILABLE`.
- Portal capture does not advertise `all-screens` or `previous-area`.
- Fallbacks are listed only when the fallback route was actually verified.

The command compatibility fixture also verifies that portal area capture never
launches `shaula-overlay`, never prepares a frozen source, and never passes a
Shaula geometry to the portal helper. It checks distinct portal cancellation and
timeout mappings.

## Lane 1: host Niri

Install the current checkout:

```bash
./dev dev-install --yes
./dev run doctor --json
./dev run capabilities list --json
./dev run preflight --json
```

Interactive checks:

```bash
./dev capture
./dev all
```

Expected:

- With `grim` present, backend is `grim-wlroots`.
- Quick and Area use Shaula's layer-shell overlay.
- Frozen mode uses an immutable captured background; live mode captures after
  overlay teardown.
- `previous-area` works after a native area capture.
- Preview opens, saves, and copies correctly.
- Copy remains available after the capture or Preview process exits.
- A second Shaula copy replaces the first provider without leaving stale owners.

Clipboard lifetime can be inspected without a separate clipboard command:

```bash
shaula clipboard copy-text --text first --json
pgrep -af shaula-clipboard-provider
shaula clipboard copy-text --text second --json
pgrep -af shaula-clipboard-provider
```

There should be one current provider after replacement. Confirm the actual
content by pasting into a normal Wayland application or with Preview's GTK/GDK
paste action.

## Lane 2: forced portal on the host

Run only when the active desktop session exposes the Screenshot portal:

```bash
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capabilities list --json
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capture fullscreen --json --no-preview
env SHAULA_CAPTURE_FORCE_PORTAL=1 ./dev run capture area --json --no-preview
```

Expected:

- Backend is `portal-screenshot`.
- Area uses the desktop portal's interactive picker.
- Shaula's overlay never appears.
- Cancelling maps to `ERR_SELECTION_CANCELLED`.
- Timeout maps to `ERR_IPC_TIMEOUT`.
- Portal unavailability maps to `ERR_CAPTURE_BACKEND_UNAVAILABLE`.

Portal UI and parent-window behavior vary by desktop. Record those differences;
do not work around them by installing another portal backend.

## Lane 3: generic GNOME/KDE portal guest

Use a full graphical Wayland session, not only a headless compositor process.
Inside the guest:

```bash
shaula doctor --json
shaula preflight --json
shaula capabilities list --json
shaula capture fullscreen --json --no-preview
shaula capture area --json --no-preview
shaula capture previous-area --json --no-preview
```

Expected:

- A working Screenshot portal reports `portal-screenshot`.
- Area selection is provided by the desktop.
- Preview, save, and Shaula-owned copy all work without `grim`.
- `previous-area` returns `ERR_CAPTURE_MODE_UNSUPPORTED`.
- A desktop without the Screenshot interface is rejected during installer or
  preflight validation rather than completing a broken installation.

Record the desktop, session type, portal framework/backend packages already
provided by the guest, prompt behavior, Shaula version, and resulting artifacts.

## Lane 4: wlroots with `grim`

Sway is the recommended small reference session. Run:

```bash
shaula doctor --json
shaula capabilities list --json
shaula capture fullscreen --json --no-preview
shaula capture all-screens --json --no-preview
shaula capture area --json --no-preview
```

Expected:

- Backend is `grim-wlroots`.
- Native Area uses Shaula's overlay.
- Fullscreen targets the focused output.
- All Screens captures the compositor layout.
- Copy uses `shaula-clipboard-provider` and survives caller exit.
- Removing `grim` causes a verified portal fallback, or a deterministic failure
  when the portal is absent.

## Installer lanes

Build a staged archive and test with isolated XDG directories.

Required smokes:

1. Generic portal route, no `grim`: install succeeds and all bundled helpers and
   resources are present.
2. Niri/wlroots with `grim`: install succeeds with backend `grim-wlroots`.
3. No native route and no Screenshot portal: install fails before placing files.
4. `--no-icon`: application icons are skipped, but Preview action icons remain.
5. Noninteractive install without explicit integration acceptance: config is
   created or kept, optional integrations are skipped without failure.
6. Setup install/remove are idempotent and symmetrical for Niri and Noctalia.
7. The installer source contains no privilege escalation or package-manager
   installation path.
8. Meson stage, archive, local install, and AUR binary package all include every
   path in `data/release-manifest.txt`.

## Historical evidence

Earlier Fedora and Ubuntu Sway guests established that real `grim-wlroots`
fullscreen and all-screens captures could produce 1280×720 PNG artifacts, and a
GNOME headless session exposed Screenshot portal version 2. Headless portal
capture remained blocked by interactive prompt/window association, while a
virtual KDE session did not expose the Screenshot interface.

Those observations remain useful environment evidence, but they predate the
Shaula-owned clipboard provider and the direct portal area orchestration. Broad
support claims require new graphical Lane 2/3 evidence with the current C/Meson
implementation.

Artifacts remain outside the repository under
`/home/fgonz/dev/shaula-lab/artifacts/`.

## Evidence template

```markdown
## YYYY-MM-DD compositor evidence

- Host/guest:
- Desktop/compositor:
- Session type:
- Capture route:
- Portal framework/backend already installed:
- Shaula build:
- Commands run:
- Expected:
- Actual:
- Clipboard lifetime/replacement:
- Artifacts:
- Notes:
```

## Minimum release gate

- Lane 0 passes.
- Lane 1 passes on the host Niri session.
- Lane 2 passes or has a documented desktop-specific blocker.
- Lane 3 passes on at least one full graphical GNOME or KDE session.
- Lane 4 passes on Sway or another compatible wlroots compositor.
- Installer lanes pass.
- `CONTEXT.md` records the remaining manual checks and latest compositor evidence.
