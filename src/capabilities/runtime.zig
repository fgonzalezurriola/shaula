const std = @import("std");
const root = @import("root");

const standalone_preflight_probe = struct {
    pub fn detectCompositor(environ: std.process.Environ) []const u8 {
        if (environ.getPosix("SHAULA_COMPOSITOR")) |value| {
            const explicit = std.mem.sliceTo(value, 0);
            if (std.ascii.eqlIgnoreCase(explicit, "niri")) return "niri";
            return "unsupported";
        }

        if (environ.getPosix("NIRI_SOCKET") != null) {
            return "niri";
        }

        return "unsupported";
    }
};

const preflight = if (@hasDecl(root, "preflight_probe_module"))
    root.preflight_probe_module
else
    standalone_preflight_probe;

pub const BackendKind = enum {
    niri_wayland_direct,
    portal_screenshot,
    stub,
};

/// Runtime-executable capture modes for the selected backend/compositor pair.
///
/// This is a strict execution contract, not a UI hint. `capture/*` commands must
/// enforce these flags before invoking backend execution.
pub const CaptureModes = struct {
    area: bool,
    fullscreen: bool,
    window: bool,
};

/// Aggregate runtime decision consumed by command handlers.
///
/// Important attributes:
/// - `compositor_supported`: whether current compositor is within supported scope.
/// - `backend`: concrete backend selected after env overrides and compositor probe.
/// - `capture`: strict mode matrix used by `enforceModeSupported`.
pub const RuntimeDecision = struct {
    compositor_supported: bool,
    backend: BackendKind,
    capture: CaptureModes,
};

/// Resolve runtime decision from environment and compositor probe.
pub fn resolve(environ: std.process.Environ) RuntimeDecision {
    const compositor_supported = std.mem.eql(u8, preflight.detectCompositor(environ), "niri");
    const backend = resolveBackend(environ);

    return .{
        .compositor_supported = compositor_supported,
        .backend = backend,
        .capture = captureModesFor(backend, compositor_supported),
    };
}

/// Backend selector with deterministic precedence.
///
/// Precedence:
/// 1. `SHAULA_CAPTURE_BACKEND=__stub__`
/// 2. `SHAULA_CAPTURE_FORCE_PORTAL=true|1`
/// 3. Niri probe -> `niri_wayland_direct`
/// 4. default -> `portal_screenshot`
pub fn resolveBackend(environ: std.process.Environ) BackendKind {
    if (environ.getPosix("SHAULA_CAPTURE_BACKEND")) |value| {
        const token = std.mem.sliceTo(value, 0);
        if (std.mem.eql(u8, token, "__stub__")) {
            return .stub;
        }
    }

    if (environ.getPosix("SHAULA_CAPTURE_FORCE_PORTAL")) |value| {
        const token = std.mem.sliceTo(value, 0);
        if (std.mem.eql(u8, token, "1") or std.ascii.eqlIgnoreCase(token, "true")) {
            return .portal_screenshot;
        }
    }

    if (std.mem.eql(u8, preflight.detectCompositor(environ), "niri")) {
        return .niri_wayland_direct;
    }

    return .portal_screenshot;
}

/// Stable backend label used in JSON responses and QA assertions.
pub fn backendLabel(kind: BackendKind) []const u8 {
    return switch (kind) {
        .niri_wayland_direct => "niri-wayland-direct",
        .portal_screenshot => "portal-screenshot",
        .stub => "__stub__",
    };
}

/// Query if a mode token (`area|fullscreen|window`) is executable.
pub fn modeSupported(capture: CaptureModes, mode: []const u8) bool {
    if (std.mem.eql(u8, mode, "area")) return capture.area;
    if (std.mem.eql(u8, mode, "fullscreen")) return capture.fullscreen;
    if (std.mem.eql(u8, mode, "window")) return capture.window;
    return false;
}

/// Ordered fallback backend labels for observability and deterministic runtime selection.
pub fn fallbacksFor(backend: BackendKind) []const []const u8 {
    return switch (backend) {
        .niri_wayland_direct => &.{"portal-screenshot"},
        .portal_screenshot => &.{},
        .stub => &.{"portal-screenshot"},
    };
}

/// Compute strict mode matrix for the resolved backend.
///
/// Note: window mode remains disabled in current scope by design.
fn captureModesFor(backend: BackendKind, compositor_supported: bool) CaptureModes {
    if (!compositor_supported) {
        return .{ .area = false, .fullscreen = false, .window = false };
    }

    return switch (backend) {
        .niri_wayland_direct => .{ .area = true, .fullscreen = true, .window = false },
        .portal_screenshot => .{ .area = true, .fullscreen = true, .window = false },
        .stub => .{ .area = false, .fullscreen = false, .window = false },
    };
}

test "runtime decision keeps window disabled for current niri backend" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    const decision = resolve(environ);
    try std.testing.expect(decision.compositor_supported);
    try std.testing.expect(decision.capture.area);
    try std.testing.expect(decision.capture.fullscreen);
    try std.testing.expect(!decision.capture.window);
}
