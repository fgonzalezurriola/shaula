const std = @import("std");

pub const Kind = enum {
    niri,
    wayland,
    unsupported,
};

pub const Detection = struct {
    kind: Kind,
    label: []const u8,
};

/// Detect compositor/runtime flavor for capture strategy selection.
///
/// Contract constraints:
/// - `label` is the stable token emitted in preflight/capabilities JSON.
/// - only `niri` is supported in current runtime scope; other Wayland
///   compositors are detected and reported but remain unsupported.
pub fn detect(environ: std.process.Environ) Detection {
    if (environ.getPosix("SHAULA_COMPOSITOR")) |value| {
        const explicit = std.mem.trim(u8, std.mem.sliceTo(value, 0), " \t\r\n");
        if (explicit.len > 0) return classifyLabel(explicit);
    }

    if (environ.getPosix("NIRI_SOCKET") != null) {
        return .{ .kind = .niri, .label = "niri" };
    }

    if (environ.getPosix("XDG_CURRENT_DESKTOP")) |value| {
        if (extractDesktopToken(std.mem.sliceTo(value, 0))) |token| {
            return classifyLabel(token);
        }
    }

    if (environ.getPosix("XDG_SESSION_DESKTOP")) |value| {
        const token = std.mem.trim(u8, std.mem.sliceTo(value, 0), " \t\r\n");
        if (token.len > 0) return classifyLabel(token);
    }

    if (environ.getPosix("WAYLAND_DISPLAY") != null) {
        return .{ .kind = .wayland, .label = "wayland" };
    }

    return .{ .kind = .unsupported, .label = "unsupported" };
}

pub fn supportedInCurrentScope(detection: Detection) bool {
    return detection.kind == .niri;
}

fn classifyLabel(label: []const u8) Detection {
    if (std.ascii.eqlIgnoreCase(label, "niri")) {
        return .{ .kind = .niri, .label = "niri" };
    }
    if (isWaylandToken(label)) {
        return .{ .kind = .wayland, .label = label };
    }
    return .{ .kind = .unsupported, .label = label };
}

fn isWaylandToken(value: []const u8) bool {
    const wayland_tokens = [_][]const u8{
        "wayland",
        "sway",
        "hyprland",
        "river",
        "wayfire",
        "weston",
        "labwc",
        "cage",
        "dwl",
    };
    for (wayland_tokens) |token| {
        if (std.ascii.eqlIgnoreCase(value, token)) return true;
    }
    return std.mem.indexOf(u8, value, "wayland") != null;
}

fn extractDesktopToken(value: []const u8) ?[]const u8 {
    var it_colon = std.mem.splitScalar(u8, value, ':');
    while (it_colon.next()) |chunk| {
        var it_semicolon = std.mem.splitScalar(u8, chunk, ';');
        while (it_semicolon.next()) |subchunk| {
            const token = std.mem.trim(u8, subchunk, " \t");
            if (token.len > 0) return token;
        }
    }
    return null;
}

test "detects niri from explicit override" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);
    const environ: std.process.Environ = .{ .block = block };
    const detected = detect(environ);
    try std.testing.expectEqual(Kind.niri, detected.kind);
    try std.testing.expectEqualStrings("niri", detected.label);
}

test "detects generic wayland compositor token" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("XDG_CURRENT_DESKTOP", "sway");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);
    const environ: std.process.Environ = .{ .block = block };
    const detected = detect(environ);
    try std.testing.expectEqual(Kind.wayland, detected.kind);
    try std.testing.expectEqualStrings("sway", detected.label);
}
