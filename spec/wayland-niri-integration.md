# Shaula Wayland and Niri Integration

See [spec/requirements.md](requirements.md), [spec/algo.md](algo.md), and
[docs/adr/0003-universal-wayland-runtime.md](../docs/adr/0003-universal-wayland-runtime.md).

## Product contract

Shaula provides capture, area selection, Preview, saving, and clipboard copy on
Wayland without asking users to select a capture or clipboard implementation.
Niri remains the primary native integration environment, but the core product is
not Niri-only.

Runtime capture selection is automatic and prerequisite-driven:

1. An explicit test/development override is honored only when its prerequisite
   is available.
2. Forced portal mode requires a verified Screenshot portal.
3. Niri and compatible wlroots sessions use `grim-wlroots` only when `grim` is
   resolvable.
4. Otherwise a verified `org.freedesktop.portal.Screenshot` interface selects
   `portal-screenshot`.
5. Otherwise capability and preflight commands report no usable capture route.

No implementation may advertise a native Niri backend while executing `grim`.
The canonical label for that route is `grim-wlroots`.

## Capability matrix

| Feature | `grim-wlroots` | `portal-screenshot` |
| --- | --- | --- |
| Quick / Area | Shaula layer-shell overlay, then region capture | Desktop portal interactive picker; no Shaula overlay |
| Fullscreen | Focused output through `grim -o` | Portal screenshot request |
| All Screens | Compositor layout through `grim` | Not advertised |
| Previous Area | Stored native geometry | Not advertised |
| Window | Capability-gated; currently not advertised | Capability-gated; currently not advertised |
| Preview / Save | Required | Required |
| Image/text copy | Bundled Shaula clipboard provider | Bundled Shaula clipboard provider |

A portal area request must not prepare a `grim` frozen image, launch
`shaula-overlay`, or pass a Shaula geometry to the portal helper.

## Runtime decisions

`src/capabilities/runtime.{c,h}` is the single backend-decision seam. Its result
distinguishes:

- compositor support;
- native overlay support;
- `grim` availability;
- Screenshot portal availability;
- one selected backend;
- whether any capture route is usable;
- the exact supported mode matrix;
- only verified fallback routes.

A supported compositor without a working backend returns
`ERR_CAPTURE_BACKEND_UNAVAILABLE`; it does not select an optimistic backend.

## Portal outcomes

The portal helper uses `org.freedesktop.portal.Request` and preserves these
outcomes through the capture command:

- user cancellation → `ERR_SELECTION_CANCELLED`;
- timeout → `ERR_IPC_TIMEOUT`;
- unavailable portal/helper → `ERR_CAPTURE_BACKEND_UNAVAILABLE`;
- other malformed or failed outcomes → deterministic mapped failure.

Helper diagnostics use stderr. Public command stdout remains a single JSON
object.

## Native overlay contract

The overlay helper handles selection UI only. Its stdout protocol is:

```json
{
  "status": "ok|cancel|error",
  "geometry": { "x": 0, "y": 0, "width": 0, "height": 0 },
  "action": "capture|copy|save|cancel",
  "error": { "code": "ERR_*", "message": "text" }
}
```

Native frozen mode captures the focused-output source before opening the
overlay, then crops that immutable source. Native live mode waits for overlay
teardown before invoking `grim`. Portal mode ignores native region-mode and
selection-overlay machinery.

## Clipboard contract

Capture, CLI clipboard commands, and Preview publish through one deep clipboard
module. The module pipes a complete PNG or UTF-8 payload to the required
`wl-copy` runtime, which claims the selection through Wayland data-control and
keeps it alive after the caller exits. Preview paste remains an asynchronous
GTK/GDK reader.

## Optional integrations

Niri keybindings/window rules and Noctalia are user-scoped optional integrations.
Rejecting or skipping either integration must not reduce capture, Preview, save,
or copy functionality.

`shaula setup` owns their state, including validation, backups, atomic writes,
idempotent installation, and symmetrical removal. Packages and installers only
place the distributed integration payload.

## Manual verification

Run the native host checks after capture, overlay, clipboard, or Niri changes:

```bash
./dev capture
./dev all
```

Use a full graphical GNOME/KDE session for portal picker verification and a Sway
or other compatible wlroots session for `grim-wlroots` verification. See
[docs/wayland-runtime-test-plan.md](../docs/wayland-runtime-test-plan.md).
