# Zig-to-C Migration Matrix

Status date: 2026-07-11

Branch: `port`

Status: production cutover complete

Every maintained production and test module is C-owned. Former Zig paths are
retained only in migration specifications and Git history.

| Area | Current C ownership | Maintained verification |
| --- | --- | --- |
| Production build/install | `meson.build`, component `meson.build` files | `./dev build`, staged Meson install, release workflow |
| CLI and orchestration | `src/main.c` | QA schemas/failure matrix, command smoke checks |
| Capture lifecycle/backends | `src/capture/command.c`, capture helpers | QA capture content/schema/mode checks, manual Wayland/Niri gates |
| Config and Niri integration | `src/config/config.c`, managed-block logic in `src/main.c` | config/Niri smoke checks, Settings process tests |
| Runtime primitives | `src/runtime/*.c` | runtime env/path/tool/helper/process/state/lock unit tests |
| Compositor and capabilities | `src/compositor/*.c`, `src/capabilities/*.c` | compositor, focused-output, capabilities unit tests |
| Preflight | `src/preflight/probe.c` | preflight unit and QA schema checks |
| Error and JSON policy | `src/errors/taxonomy.c`, `src/cli/json.c` | taxonomy/JSON unit tests and canonical fixture |
| Notification request model | `src/notify/request.c` | notification request unit tests |
| Preview helper | `src/preview/*.c` | geometry, image I/O, clipboard, notify, result unit tests |
| Settings helper | `src/settings/*.c` | config and process unit tests |
| Overlay helper | `src/overlay/*.c` | helper protocol QA and manual interactive checks |
| Crop/portal helpers | `src/capture/native_crop_image.c`, `src/capture/portal_screenshot.c` | production build/install and capture QA |
| QA capture helper | `scripts/qa/fake_runtime_capture_helper.c` | fast QA failure/content matrices |

## Final gates

- `rg --files -g '*.zig' -g 'build.zig*'` returns no files.
- Production and QA scripts do not invoke `zig` or reference `zig-out`.
- `./dev check` passes the maintained Meson tests.
- `./dev qa` passes the non-intrusive product matrix.
- A staged Meson install contains all six executables and integration assets.
- Strict warning, sanitizer, and live Wayland/Niri checks remain release gates
  appropriate to the changed behavior.
