const std = @import("std");
const capture_types = @import("types.zig");

test "mode string mapping is deterministic" {
    try std.testing.expectEqualStrings("area", capture_types.modeString(.area));
    try std.testing.expectEqualStrings("fullscreen", capture_types.modeString(.fullscreen));
    try std.testing.expectEqualStrings("window", capture_types.modeString(.window));
}

test "area geometry conversion from selection is deterministic" {
    const converted = capture_types.areaGeometryFromSelection(.{ .x = 10, .y = -20, .width = 640, .height = 360 }) orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 10), converted.x);
    try std.testing.expectEqual(@as(i32, -20), converted.y);
    try std.testing.expectEqual(@as(u32, 640), converted.width);
    try std.testing.expectEqual(@as(u32, 360), converted.height);
}

test "fractional output local normalization is deterministic" {
    const normalized = capture_types.normalizeOutputLocalGeometry(
        .{ .x = 100, .y = 60, .width = 250, .height = 125 },
        .{ .x = 1920, .y = 0, .scale_numerator = 4, .scale_denominator = 5 },
    );

    try std.testing.expectEqual(@as(i32, 2045), normalized.x);
    try std.testing.expectEqual(@as(i32, 75), normalized.y);
    try std.testing.expectEqual(@as(u32, 313), normalized.width);
    try std.testing.expectEqual(@as(u32, 156), normalized.height);
}
