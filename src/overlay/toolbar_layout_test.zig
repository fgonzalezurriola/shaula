const std = @import("std");
const toolbar_layout = @import("toolbar_layout.zig");

test "toolbar prefers below selection when it fits" {
    const result = toolbar_layout.compute(
        .{ .x = 0, .y = 0, .width = 800, .height = 600 },
        .{ .x = 200, .y = 120, .width = 260, .height = 160 },
        .{ .width = 240, .height = 48 },
        null,
        null,
        .{},
    );
    try std.testing.expectEqual(toolbar_layout.Placement.below, result.placement);
    try std.testing.expect(result.position.y > 280);
}

test "toolbar moves above when below does not fit" {
    const result = toolbar_layout.compute(
        .{ .x = 0, .y = 0, .width = 800, .height = 600 },
        .{ .x = 200, .y = 430, .width = 260, .height = 130 },
        .{ .width = 240, .height = 48 },
        null,
        null,
        .{},
    );
    try std.testing.expectEqual(toolbar_layout.Placement.above, result.placement);
    try std.testing.expect(result.position.y < 430);
}

test "toolbar clamps inside visible output" {
    const result = toolbar_layout.compute(
        .{ .x = 100, .y = 50, .width = 500, .height = 300 },
        .{ .x = 540, .y = 260, .width = 50, .height = 60 },
        .{ .width = 220, .height = 52 },
        null,
        null,
        .{},
    );
    try std.testing.expect(result.position.x >= 112);
    try std.testing.expect(result.position.x + 220 <= 588);
    try std.testing.expect(result.position.y >= 62);
    try std.testing.expect(result.position.y + 52 <= 338);
}

test "toolbar keeps previous position across tiny movement" {
    const previous = toolbar_layout.Point{ .x = 210, .y = 296 };
    const result = toolbar_layout.compute(
        .{ .x = 0, .y = 0, .width = 800, .height = 600 },
        .{ .x = 199, .y = 118, .width = 263, .height = 160 },
        .{ .width = 240, .height = 48 },
        null,
        previous,
        .{},
    );
    try std.testing.expectEqual(previous.x, result.position.x);
    try std.testing.expectEqual(previous.y, result.position.y);
}
