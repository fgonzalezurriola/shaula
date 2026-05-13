const std = @import("std");

pub const CaptureMode = enum {
    quick,
    area,
    fullscreen,
    all_screens,
    focused,
    window,
    previous_area,
    all_in_one,
};

pub const RuntimeCaptureMode = enum {
    area,
    current_output,
    all_outputs,
    window,
};

pub const RegionCaptureMode = enum {
    live,
    frozen,

    pub fn asString(mode: RegionCaptureMode) []const u8 {
        return switch (mode) {
            .live => "live",
            .frozen => "frozen",
        };
    }
};

pub fn parseRegionCaptureMode(token: []const u8) ?RegionCaptureMode {
    if (std.mem.eql(u8, token, "live")) return .live;
    if (std.mem.eql(u8, token, "frozen")) return .frozen;
    return null;
}

/// Central user-facing capture mode model.
///
/// Contract constraints:
/// - CLI tokens stay stable even if backend execution uses a different runtime
///   mode (`previous-area` currently reuses area capture underneath).
/// - `all-in-one` is a legacy token for the Capture overlay flow and still
///   executes through the area lane.
/// - `focused` is a compatibility CLI alias for the current-output runtime lane.
pub fn parseCliToken(token: []const u8) ?CaptureMode {
    if (std.mem.eql(u8, token, "quick")) return .quick;
    if (std.mem.eql(u8, token, "area")) return .area;
    if (std.mem.eql(u8, token, "fullscreen")) return .fullscreen;
    if (std.mem.eql(u8, token, "all-screens")) return .all_screens;
    if (std.mem.eql(u8, token, "focused")) return .focused;
    if (std.mem.eql(u8, token, "window")) return .window;
    if (std.mem.eql(u8, token, "previous-area")) return .previous_area;
    if (std.mem.eql(u8, token, "all-in-one")) return .all_in_one;
    return null;
}

pub fn cliToken(mode: CaptureMode) []const u8 {
    return switch (mode) {
        .quick => "quick",
        .area => "area",
        .fullscreen => "fullscreen",
        .all_screens => "all-screens",
        .focused => "focused",
        .window => "window",
        .previous_area => "previous-area",
        .all_in_one => "all-in-one",
    };
}

pub fn runtimeMode(mode: CaptureMode) RuntimeCaptureMode {
    return switch (mode) {
        .quick, .area, .previous_area, .all_in_one => .area,
        .fullscreen, .focused => .current_output,
        .all_screens => .all_outputs,
        .window => .window,
    };
}

pub fn runtimeModeToken(mode: RuntimeCaptureMode) []const u8 {
    return switch (mode) {
        .area => "area",
        .current_output => "current-output",
        .all_outputs => "all-outputs",
        .window => "window",
    };
}

pub fn backendModeToken(mode: CaptureMode) ?[]const u8 {
    return switch (mode) {
        .quick, .area, .previous_area, .all_in_one => "area",
        .fullscreen => "fullscreen",
        .all_screens => "all-screens",
        .focused => "focused",
        .window => "window",
    };
}

pub fn requiresInteractiveSelection(mode: CaptureMode) bool {
    return switch (mode) {
        .quick, .area, .all_in_one => true,
        .fullscreen, .all_screens, .focused, .window, .previous_area => false,
    };
}

test "cli token parsing keeps dashed modes deterministic" {
    try std.testing.expectEqual(CaptureMode.quick, parseCliToken("quick") orelse return error.TestExpectedEqual);
    try std.testing.expectEqual(CaptureMode.area, parseCliToken("area") orelse return error.TestExpectedEqual);
    try std.testing.expectEqual(CaptureMode.all_screens, parseCliToken("all-screens") orelse return error.TestExpectedEqual);
    try std.testing.expectEqual(CaptureMode.previous_area, parseCliToken("previous-area") orelse return error.TestExpectedEqual);
    try std.testing.expectEqual(CaptureMode.all_in_one, parseCliToken("all-in-one") orelse return error.TestExpectedEqual);
}

test "backend mode token keeps previous-area on area runtime lane" {
    try std.testing.expectEqualStrings("area", backendModeToken(.quick) orelse return error.TestExpectedEqual);
    try std.testing.expectEqualStrings("area", backendModeToken(.previous_area) orelse return error.TestExpectedEqual);
    try std.testing.expectEqualStrings("area", backendModeToken(.all_in_one) orelse return error.TestExpectedEqual);
    try std.testing.expectEqualStrings("fullscreen", backendModeToken(.fullscreen) orelse return error.TestExpectedEqual);
    try std.testing.expectEqualStrings("all-screens", backendModeToken(.all_screens) orelse return error.TestExpectedEqual);
}
