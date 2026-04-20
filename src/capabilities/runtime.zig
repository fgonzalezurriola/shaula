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

pub const CaptureModes = struct {
    area: bool,
    fullscreen: bool,
    window: bool,
};

pub const RuntimeDecision = struct {
    compositor_supported: bool,
    backend: BackendKind,
    capture: CaptureModes,
};

pub fn resolve(environ: std.process.Environ) RuntimeDecision {
    const compositor_supported = std.mem.eql(u8, preflight.detectCompositor(environ), "niri");
    const backend = resolveBackend(environ);

    return .{
        .compositor_supported = compositor_supported,
        .backend = backend,
        .capture = captureModesFor(backend, compositor_supported),
    };
}

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

pub fn backendLabel(kind: BackendKind) []const u8 {
    return switch (kind) {
        .niri_wayland_direct => "niri-wayland-direct",
        .portal_screenshot => "portal-screenshot",
        .stub => "__stub__",
    };
}

pub fn modeSupported(capture: CaptureModes, mode: []const u8) bool {
    if (std.mem.eql(u8, mode, "area")) return capture.area;
    if (std.mem.eql(u8, mode, "fullscreen")) return capture.fullscreen;
    if (std.mem.eql(u8, mode, "window")) return capture.window;
    return false;
}

pub fn fallbacksFor(backend: BackendKind) []const []const u8 {
    return switch (backend) {
        .niri_wayland_direct => &.{"portal-screenshot"},
        .portal_screenshot => &.{},
        .stub => &.{"portal-screenshot"},
    };
}

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
