# Shaula Developer Guide

This guide covers how to build, test, and develop Shaula.

## Prerequisites

- **Zig 0.16.0** - Toolchain must match exactly
- **Niri** - Wayland compositor (required for capture)
- **wl-clipboard** - Clipboard utilities (`wl-paste`, `wl-copy`)
- **grim** - Screenshot tool
- **jq** - JSON parser

## Building

```bash
# Build the application
zig build

# The binary is at zig-out/bin/shaula
```

## Development Tests

### Run Full QA Suite

```bash
bash scripts/qa/run-all-tests.sh
```

This executes all QA checks in sequence. See individual scripts below.

### Individual QA Scripts

| Script | Purpose |
|--------|---------|
| `scripts/qa/preflight-wayland-niri.sh` | Preflight check: compositor, socket, IPC readiness |
| `scripts/qa/test-daemon-lifecycle.sh` | Daemon start/status/stop IPC cycle |
| `scripts/qa validate-future-feature-matrix.sh` | Verify feature spec matrix |
| `scripts/qa/run-e2e-niri.sh` | End-to-end capture test |
| `scripts/qa/run-integration-tests.sh` | Integration tests (capture integrity, capabilities, overlay) |
| `scripts/qa/release-readiness-capture-fix.sh` | Release readiness checklist |

### Preflight Check

```bash
# Manual preflight
bash scripts/qa/preflight-wayland-niri.sh

# Programmatic preflight (returns exit code)
./zig-out/bin/shaula preflight --json
```

### Daemon Lifecycle

```bash
# Start daemon
./zig-out/bin/shaula daemon start --json

# Check status
./zig-out/bin/shaula daemon status --json

# Stop daemon
./zig-out/bin/shaula daemon stop --json
```

## CLI Commands

### Capture

```bash
# Area selection with overlay (interactive)
./zig-out/bin/shaula capture area --json

# Dry-run (returns deterministic geometry without actual capture)
./zig-out/bin/shaula capture area --json --dry-run

# Simulate cancel (tests error path)
./zig-out/bin/shaula capture area --json --simulate-cancel

# Save to default ~/Pictures/Shaula
./zig-out/bin/shaula capture area --json

# Save to specific path
./zig-out/bin/shaula capture area --json --output /tmp/screenshot.png

# Copy to clipboard (implies --json)
./zig-out/bin/shaula capture area --json --copy
```

### Capabilities

```bash
# List compositor capabilities
./zig-out/bin/shaula capabilities list --json
```

### History

```bash
# List past captures
./zig-out/bin/shaula history list --json

# Show specific capture metadata
./zig-out/bin/shaula history show --json --id <uuid>
```

### Daemon

```bash
# Start background daemon
./zig-out/bin/shaula daemon start --json [--socket /path/to/socket]

# Check daemon status
./zig-out/bin/shaula daemon status --json [--socket /path/to/socket]

# Stop daemon
./zig-out/bin/shaula daemon stop --json [--socket /path/to/socket]
```

## Required Environment Variables

```bash
# Wayland socket name (required for Niri)
export WAYLAND_DISPLAY=niri

# Niri socket path (optional, for multi-seat)
export NIRI_SOCKET=/run/user/1000/niri-0.sock
```

## Troubleshooting

### "ERR_UNSUPPORTED_COMPOSITOR"

Only Niri is supported. Verify you're running Niri:

```bash
echo $WAYLAND_DISPLAY
# Should show your Niri socket name
```

### Daemon Won't Start

Check if socket exists and is writable:

```bash
ls -la /run/user/1000/niri*.sock
```

### Capture Fails Silently

Run preflight first:

```bash
./zig-out/bin/shaula preflight --json | jq
```

### "ERR_CAPTURE_BACKEND_UNAVAILABLE"

Shaula is now **real-capture by default** (Wayland-first). For Niri this requires `grim` available at runtime.

Check `grim`:

```bash
command -v grim
```

If it is missing, install it with your distro package manager and retry capture.

For deterministic QA in environments without `grim`, you can opt into the synthetic helper explicitly:

```bash
export SHAULA_RUNTIME_CAPTURE_HELPER="$(pwd)/scripts/qa/fake_runtime_capture_helper.py"
./zig-out/bin/shaula capture area --json --output /tmp/qa-capture.png
```

## Project Structure

```
src/
├── main.zig           # CLI entry point
├── daemon/
│   └── server.zig     # IPC daemon
├── capture/
│   └── command.zig   # Capture logic
├── backends/
│   └── capture_backend.zig  # Niri capture impl
├── pipeline/
│   └── post_capture.zig    # Image processing
└── history/
    └── command.zig     # History tracking

scripts/qa/
├── run-all-tests.sh
├── preflight-wayland-niri.sh
├── test-daemon-lifecycle.sh
├── validate-future-feature-matrix.sh
└── run-e2e-niri.sh
```

## Testing New Changes

1. Rebuild: `zig build`
2. Run preflight: `bash scripts/qa/preflight-wayland-niri.sh`
3. Run full suite: `bash scripts/qa/run-all-tests.sh`
4. Manual test: `./zig-out/bin/shaula capture area --json`
