const std = @import("std");

pub const OverlayStrategy = enum {
    auto,
    gtk4_layer_shell,
    raylib,
    raylib_clay,
};

/// Parses the overlay helper strategy selector.
///
/// Contract constraints:
/// - unknown values resolve to `auto` so config typos do not silently select a
///   heavyweight or unavailable UI backend.
/// - tokens are stable because QA and latency comparisons depend on them.
pub fn parse(raw: ?[]const u8) OverlayStrategy {
    const value = raw orelse return .auto;
    if (std.mem.eql(u8, value, "gtk4-layer-shell") or std.mem.eql(u8, value, "gtk4") or std.mem.eql(u8, value, "gtk")) return .gtk4_layer_shell;
    if (std.mem.eql(u8, value, "raylib")) return .raylib;
    if (std.mem.eql(u8, value, "raylib-clay") or std.mem.eql(u8, value, "clay")) return .raylib_clay;
    return .auto;
}

pub fn token(strategy: OverlayStrategy) []const u8 {
    return switch (strategy) {
        .auto => "auto",
        .gtk4_layer_shell => "gtk4-layer-shell",
        .raylib => "raylib",
        .raylib_clay => "raylib-clay",
    };
}

test "overlay strategy parser accepts stable tokens" {
    try std.testing.expectEqual(OverlayStrategy.auto, parse(null));
    try std.testing.expectEqual(OverlayStrategy.gtk4_layer_shell, parse("gtk4-layer-shell"));
    try std.testing.expectEqual(OverlayStrategy.gtk4_layer_shell, parse("gtk4"));
    try std.testing.expectEqual(OverlayStrategy.raylib, parse("raylib"));
    try std.testing.expectEqual(OverlayStrategy.raylib_clay, parse("raylib-clay"));
    try std.testing.expectEqual(OverlayStrategy.auto, parse("unknown"));
}
