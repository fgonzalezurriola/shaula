# Shaula Developer Guide

Shaula is a Niri/Wayland capture CLI with deterministic JSON contracts. The
developer workflow is intentionally routed through `./dev` so common commands do
not require memorizing QA script names or environment flags.

## Quick Start

```bash
./dev doctor
./dev build
./dev test
./dev capture
```

Useful commands:

```bash
./dev help
./dev check
./dev qa
./dev bench
./dev strategies
```

Commands that open or repeatedly exercise the UI are explicit:

```bash
./dev overlay
./dev qa-ui
./dev bench-ui
./dev strategies-ui
```

## Requirements

- Zig `0.16.0`
- Niri
- `cc`
- `pkg-config`
- `gtk4`
- `gtk4-layer-shell-0`
- `gdk-pixbuf-2.0`
- `cairo`
- `grim`
- `wl-copy`
- `wl-paste`
- `jq`

Run `./dev doctor` to check the local machine.

## App Commands

`./dev run` forwards to the compiled Shaula binary after building:

```bash
./dev run preflight --json
./dev run capabilities list --json
./dev run capture area --json --aspect 16:9
./dev run capture all-in-one --json
./dev run capture previous-area --json
./dev run preview ~/Pictures/Shaula/example.png --json
./dev run history list --json
./dev run errors list --json
```

Shortcuts:

```bash
./dev preflight
./dev capture
./dev all
./dev errors
```

## QA Profiles

Non-intrusive by default:

```bash
./dev qa
./dev qa-full
./dev bench
./dev strategies
```

Intrusive UI opt-in:

```bash
./dev qa-ui
./dev bench-ui
./dev strategies-ui
```

The non-intrusive overlay latency report may show `0.0ms` with
`ERR_PERF_INTRUSIVE_UI_DISABLED_BY_POLICY`; that is a degraded policy result, not
a real first-paint measurement. Use `./dev bench-ui` for real overlay timing.

## Overlay Notes

- Production overlay backend is `gtk4-layer-shell`.
- Legacy experimental UI code paths were removed from the product tree to keep the
  overlay architecture focused on native Wayland layer-shell behavior.
- If the native helper is unavailable, Shaula fails deterministically with
  `ERR_OVERLAY_UNAVAILABLE`; there is no secondary selector fallback.
- `capture area` and `capture all-in-one` copy to the Wayland clipboard by
  default.

## Troubleshooting

`ERR_UNSUPPORTED_COMPOSITOR`

```bash
./dev doctor
```

Shaula v1 is Niri-first. Confirm `SHAULA_COMPOSITOR`, `WAYLAND_DISPLAY`, and
`NIRI_SOCKET`.

`ERR_CAPTURE_BACKEND_UNAVAILABLE`

```bash
command -v grim
```

`ERR_OVERLAY_UNAVAILABLE`

```bash
./dev build
test -x ./zig-out/bin/shaula-overlay && echo helper-ok
```

`ERR_CLIPBOARD_COPY_FAILED`

```bash
command -v wl-copy
```

## Repo Shape

```text
src/          Zig implementation and native overlay shim boundary
scripts/qa/   QA, benchmarks, and evidence scripts
spec/         Product and architecture contracts
integrations/ Optional integration adapters
```
