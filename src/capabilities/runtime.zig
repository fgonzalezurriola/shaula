const std = @import("std");
const backend_contract = @import("../backends/capture_backend_contract.zig");
const compositor_runtime = @import("../compositor/runtime.zig");
const env = @import("../runtime/env.zig");
const portal_screenshot = @import("../backends/portal_screenshot.zig");
const tool_lookup = @import("../runtime/tool_lookup.zig");

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

    pub fn backendUsedLabel(self: RuntimeDecision) []const u8 {
        return backendLabel(self.backend);
    }

    pub fn usesPortalBackend(self: RuntimeDecision) bool {
        return self.backend == .portal_screenshot;
    }

    pub fn degradedBackend(self: RuntimeDecision) bool {
        return self.usesPortalBackend();
    }

    pub fn shouldBypassOverlaySelection(self: RuntimeDecision) bool {
        return self.usesPortalBackend() or !self.overlay_supported;
    }

    pub fn portalSelectionAvailable(self: RuntimeDecision) bool {
        return self.usesPortalBackend() or self.portal_available;
    }

    pub fn previousAreaSupported(self: RuntimeDecision) bool {
        return self.backend != .portal_screenshot;
    }

    pub fn selectPortalFallback(self: *RuntimeDecision) void {
        self.backend = .portal_screenshot;
        self.capture = captureModesFor(self.backend, self.compositor_supported);
    }

    pub fn withPortalFallback(self: *RuntimeDecision) void {
        self.selectPortalFallback();
    }
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
    if (env.trimmed(environ, "SHAULA_CAPTURE_BACKEND")) |token| {
        if (std.mem.eql(u8, token, backend_contract.backend_stub)) {
            return .stub;
        }
        if (std.mem.eql(u8, token, backend_contract.backend_portal_screenshot)) return .portal_screenshot;
        if (std.mem.eql(u8, token, backend_contract.backend_grim_wlroots)) return .grim_wlroots;
        if (std.mem.eql(u8, token, backend_contract.backend_niri_wayland_direct)) return .niri_wayland_direct;
    }

    if (env.flagEnabled(environ, "SHAULA_CAPTURE_FORCE_PORTAL")) {
        return .portal_screenshot;
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
    return tool_lookup.grimPath(io) != null;
}

/// Stable backend label used in JSON responses and QA assertions.
pub fn backendLabel(kind: BackendKind) []const u8 {
    return switch (kind) {
        .niri_wayland_direct => backend_contract.backend_niri_wayland_direct,
        .grim_wlroots => backend_contract.backend_grim_wlroots,
        .portal_screenshot => backend_contract.backend_portal_screenshot,
        .stub => backend_contract.backend_stub,
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
        .niri_wayland_direct => &.{backend_contract.backend_portal_screenshot},
        .grim_wlroots => &.{backend_contract.backend_portal_screenshot},
        .portal_screenshot => &.{},
        .stub => &.{backend_contract.backend_portal_screenshot},
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
