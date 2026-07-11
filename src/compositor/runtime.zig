const std = @import("std");
const c = @cImport({
    @cInclude("runtime/env.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn spanSlice(span: c.ShaulaEnvSpan) []const u8 {
    return span.data[0..span.length];
}

fn envSlice(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
    if (c.shaula_env_value_slice(envValue(environ, key), &result) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return spanSlice(result);
}

fn envTrimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
    if (c.shaula_env_value_trimmed(envValue(environ, key), &result) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return spanSlice(result);
}

fn firstDesktopToken(value: []const u8) ?[]const u8 {
    var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
    const input: c.ShaulaEnvSpan = .{ .data = value.ptr, .length = value.len };
    if (c.shaula_env_first_desktop_token(input, &result) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return spanSlice(result);
}

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
/// - generic Wayland support is gated by portal availability in runtime
///   capabilities, while overlay remains limited to Niri/wlroots tokens.
pub fn detect(environ: std.process.Environ) Detection {
    if (envTrimmed(environ, "SHAULA_COMPOSITOR")) |explicit| {
        return classifyLabel(explicit);
    }

    if (environ.getPosix("NIRI_SOCKET") != null) {
        return .{ .kind = .niri, .label = "niri" };
    }

    if (envSlice(environ, "XDG_CURRENT_DESKTOP")) |value| {
        if (firstDesktopToken(value)) |token| {
            return classifyLabel(token);
        }
    }

    if (envTrimmed(environ, "XDG_SESSION_DESKTOP")) |token| {
        return classifyLabel(token);
    }

    if (environ.getPosix("WAYLAND_DISPLAY") != null) {
        return .{ .kind = .wayland, .label = "wayland" };
    }

    return .{ .kind = .unsupported, .label = "unsupported" };
}

pub fn supportedInCurrentScope(detection: Detection, portal_available: bool) bool {
    return detection.kind == .niri or isWlroots(detection) or (detection.kind == .wayland and portal_available);
}

pub fn overlaySupported(detection: Detection) bool {
    return detection.kind == .niri or isWlroots(detection);
}

pub fn isWlroots(detection: Detection) bool {
    if (detection.kind == .niri) return false;
    return isWlrootsToken(detection.label);
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
        "gnome",
        "gnome-shell",
        "kde",
        "plasma",
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

fn isWlrootsToken(value: []const u8) bool {
    const wlroots_tokens = [_][]const u8{
        "sway",
        "hyprland",
        "river",
        "wayfire",
        "labwc",
        "cage",
        "dwl",
    };
    for (wlroots_tokens) |token| {
        if (std.ascii.eqlIgnoreCase(value, token)) return true;
    }
    return false;
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
