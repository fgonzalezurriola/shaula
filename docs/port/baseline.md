# Zig-to-C Port Baseline and Cutover Record

Initial baseline: 2026-07-10

Cutover: 2026-07-11

Branch: `port`

Authority: `spec/zig-to-c-port.md`

## Initial state

The migration began with Zig owning the production build and top-level runtime,
while C already owned GTK helpers and a growing set of leaf modules. The
baseline froze the six-executable install inventory, JSON/error contracts,
runtime/backend decisions, helper protocols, configuration behavior, and
Wayland/Niri manual gates.

The initial host used Meson 1.11.1, Ninja 1.13.2, GCC 16.1.1, Clang 22.1.8,
GLib 2.88.2, JSON-GLib 1.10.8, GTK 4.22.4, GDK Pixbuf 2.44.7, Cairo 1.18.4,
Pango 1.58.0, and GTK4 layer shell 1.3.0. These observations are not
minimum-version claims.

## Final state

- Meson is the authoritative production, test, install, packaging, and CI build.
- All maintained implementation and test sources are C; no `.zig` source,
  `build.zig`, Zig version pin, or Zig CI setup remains.
- The installed inventory remains `shaula`, `shaula-overlay`, `shaula-preview`,
  `shaula-settings`, `shaula-crop-image`, and `shaula-portal-screenshot`, plus
  icons and the Noctalia integration.
- `./dev check` runs the complete maintained Meson suite, including a
  non-intrusive top-level command compatibility fixture.
- `./dev qa` runs deterministic non-intrusive command/schema/failure checks.
- `./dev port-check` and `./dev port-check-asan` provide strict warning and
  sanitizer lanes.
- Release staging and the AUR source package use Meson/Ninja. `build-stage/` is
  generated on demand, ignored, and not a version-controlled payload.

## Preserved contract fixtures

`tests/fixtures/port/errors-list.json` remains the timestamp-free canonical
error-taxonomy fixture. The QA scripts under `scripts/qa/` exercise public JSON
schemas, deterministic failure mapping, runtime capture content, overlay helper
protocols, history consistency, Noctalia actions, and release readiness.

## Host-dependent gates

Automated tests do not replace a live Wayland/Niri session. Capture, overlay,
clipboard, GTK, and compositor changes require the targeted manual checks in
`AGENTS.md`; interactive overlay work requires `./dev capture` and `./dev all`.

The detailed sequence of intermediate ownership slices remains available in Git
history. `docs/port/migration-matrix.md` records the final ownership map.
