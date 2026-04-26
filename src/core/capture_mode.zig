const std = @import("std");

pub const CaptureMode = enum {
    area,
    fullscreen,
    window,
    previous_area,
    all_in_one,
};

/// Central user-facing capture mode model.
///
/// Contract constraints:
/// - CLI tokens stay stable even if backend execution uses a different runtime
///   mode (`previous-area` currently reuses area capture underneath).
/// - `all-in-one` remains representable here before it becomes a public CLI
///   contract, avoiding mode growth from leaking into backend-specific enums.
pub fn parseCliToken(token: []const u8) ?CaptureMode {
    if (std.mem.eql(u8, token, "area")) return .area;
    if (std.mem.eql(u8, token, "fullscreen")) return .fullscreen;
    if (std.mem.eql(u8, token, "window")) return .window;
    if (std.mem.eql(u8, token, "previous-area")) return .previous_area;
    return null;
}

pub fn cliToken(mode: CaptureMode) []const u8 {
    return switch (mode) {
        .area => "area",
        .fullscreen => "fullscreen",
        .window => "window",
        .previous_area => "previous-area",
        .all_in_one => "all-in-one",
    };
}

pub fn backendModeToken(mode: CaptureMode) ?[]const u8 {
    return switch (mode) {
        .area => "area",
        .fullscreen => "fullscreen",
        .window => "window",
        .previous_area => "area",
        .all_in_one => null,
    };
}

pub fn requiresInteractiveSelection(mode: CaptureMode) bool {
    return switch (mode) {
        .area, .all_in_one => true,
        .fullscreen, .window, .previous_area => false,
    };
}

test "cli token parsing keeps dashed modes deterministic" {
    try std.testing.expectEqual(CaptureMode.area, parseCliToken("area") orelse return error.TestExpectedEqual);
    try std.testing.expectEqual(CaptureMode.previous_area, parseCliToken("previous-area") orelse return error.TestExpectedEqual);
    try std.testing.expect(parseCliToken("all-in-one") == null);
}

test "backend mode token keeps previous-area on area runtime lane" {
    try std.testing.expectEqualStrings("area", backendModeToken(.previous_area) orelse return error.TestExpectedEqual);
    try std.testing.expect(backendModeToken(.all_in_one) == null);
}
