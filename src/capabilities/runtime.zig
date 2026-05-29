const std = @import("std");
const compositor_runtime = @import("../compositor/runtime.zig");
const portal_screenshot = @import("../backends/portal_screenshot.zig");

pub const BackendKind = enum {
    niri_wayland_direct,
    grim_wlroots,
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
    all_screens: bool,
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
    overlay_supported: bool,
    backend: BackendKind,
    capture: CaptureModes,
    portal_available: bool = false,
    portal_window_capable: bool = false,
    compositor: compositor_runtime.Detection,
};

/// Resolve runtime decision from environment and compositor probe.
pub fn resolve(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) RuntimeDecision {
    const compositor = compositor_runtime.detect(environ);
    const portal = portal_screenshot.detectCapabilities(allocator, io, environ);
    const compositor_supported = compositor_runtime.supportedInCurrentScope(compositor, portal.available);
    const backend = resolveBackend(environ, compositor, portal.available, grimAvailable(io));

    return .{
        .compositor_supported = compositor_supported,
        .overlay_supported = compositor_runtime.overlaySupported(compositor),
        .backend = backend,
        .capture = captureModesFor(backend, compositor_supported),
        .portal_available = portal.available,
        .portal_window_capable = portal.window_capable,
        .compositor = compositor,
    };
}

/// Backend selector with deterministic precedence.
///
/// Precedence:
/// 1. `SHAULA_CAPTURE_BACKEND=__stub__`
/// 2. `SHAULA_CAPTURE_FORCE_PORTAL=true|1`
/// 3. Niri probe -> `niri_wayland_direct`
/// 4. wlroots probe -> `grim_wlroots`
/// 5. generic Wayland with portal -> `portal_screenshot`
pub fn resolveBackend(environ: std.process.Environ, compositor: compositor_runtime.Detection, portal_available: bool, grim_available: bool) BackendKind {
    if (environ.getPosix("SHAULA_CAPTURE_BACKEND")) |value| {
        const token = std.mem.sliceTo(value, 0);
        if (std.mem.eql(u8, token, "__stub__")) {
            return .stub;
        }
        if (std.mem.eql(u8, token, "portal-screenshot")) return .portal_screenshot;
        if (std.mem.eql(u8, token, "grim-wlroots")) return .grim_wlroots;
        if (std.mem.eql(u8, token, "niri-wayland-direct")) return .niri_wayland_direct;
    }

    if (environ.getPosix("SHAULA_CAPTURE_FORCE_PORTAL")) |value| {
        const token = std.mem.sliceTo(value, 0);
        if (std.mem.eql(u8, token, "1") or std.ascii.eqlIgnoreCase(token, "true")) {
            return .portal_screenshot;
        }
    }

    if (compositor.kind == .niri) {
        return .niri_wayland_direct;
    }

    if (compositor_runtime.isWlroots(compositor)) {
        if (grim_available) return .grim_wlroots;
        if (portal_available) return .portal_screenshot;
        return .grim_wlroots;
    }

    if (portal_available and compositor.kind == .wayland) return .portal_screenshot;

    return .portal_screenshot;
}

fn grimAvailable(io: std.Io) bool {
    const paths = [_][]const u8{ "/usr/bin/grim", "/bin/grim", "/usr/local/bin/grim" };
    for (paths) |path| {
        std.Io.Dir.accessAbsolute(io, path, .{}) catch continue;
        return true;
    }
    return false;
}

/// Stable backend label used in JSON responses and QA assertions.
pub fn backendLabel(kind: BackendKind) []const u8 {
    return switch (kind) {
        .niri_wayland_direct => "niri-wayland-direct",
        .grim_wlroots => "grim-wlroots",
        .portal_screenshot => "portal-screenshot",
        .stub => "__stub__",
    };
}

/// Query if a public or compatibility mode token is executable.
pub fn modeSupported(capture: CaptureModes, mode: []const u8) bool {
    if (std.mem.eql(u8, mode, "all-in-one")) return capture.area;
    if (std.mem.eql(u8, mode, "quick")) return capture.area;
    if (std.mem.eql(u8, mode, "area")) return capture.area;
    if (std.mem.eql(u8, mode, "fullscreen")) return capture.fullscreen;
    if (std.mem.eql(u8, mode, "all-screens")) return capture.all_screens;
    if (std.mem.eql(u8, mode, "focused")) return capture.fullscreen;
    if (std.mem.eql(u8, mode, "window")) return capture.window;
    return false;
}

/// Ordered fallback backend labels for observability and deterministic runtime selection.
pub fn fallbacksFor(backend: BackendKind) []const []const u8 {
    return switch (backend) {
        .niri_wayland_direct => &.{"portal-screenshot"},
        .grim_wlroots => &.{"portal-screenshot"},
        .portal_screenshot => &.{},
        .stub => &.{"portal-screenshot"},
    };
}

/// Compute strict mode matrix for the resolved backend.
///
/// Note: window mode remains disabled in current scope by design.
fn captureModesFor(backend: BackendKind, compositor_supported: bool) CaptureModes {
    if (!compositor_supported) {
        return .{ .area = false, .fullscreen = false, .all_screens = false, .window = false };
    }

    return switch (backend) {
        .niri_wayland_direct => .{ .area = true, .fullscreen = true, .all_screens = true, .window = false },
        .grim_wlroots => .{ .area = true, .fullscreen = true, .all_screens = true, .window = false },
        .portal_screenshot => .{ .area = true, .fullscreen = true, .all_screens = true, .window = false },
        .stub => .{ .area = false, .fullscreen = false, .all_screens = false, .window = false },
    };
}

test "runtime decision keeps window disabled for current niri backend" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    const decision = resolve(std.testing.allocator, std.testing.io, environ);
    try std.testing.expect(decision.compositor_supported);
    try std.testing.expect(decision.capture.area);
    try std.testing.expect(decision.capture.fullscreen);
    try std.testing.expect(decision.capture.all_screens);
    try std.testing.expect(!decision.capture.window);
}
