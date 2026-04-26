const std = @import("std");
const toolbar_layout = @import("toolbar_layout.zig");

pub const ToolbarState = struct {
    size: toolbar_layout.Size = .{ .width = 312, .height = 52 },
    position: ?toolbar_layout.Point = null,
    placement: toolbar_layout.Placement = .below,
};

pub const AllInOneSession = struct {
    output: toolbar_layout.Rect,
    selection_rect: ?toolbar_layout.SelectionRect = null,
    toolbar: ToolbarState = .{},

    pub fn init(output: toolbar_layout.Rect, persisted_toolbar: ?toolbar_layout.Point) AllInOneSession {
        return .{
            .output = output,
            .toolbar = .{ .position = persisted_toolbar },
        };
    }

    /// Updates active selection geometry and derives a stable toolbar position.
    ///
    /// Contract constraints:
    /// - selection geometry remains independent from toolbar policy.
    /// - toolbar placement is recomputed only from explicit session state and
    ///   visible output bounds, yielding deterministic positions for QA.
    pub fn updateSelection(self: *AllInOneSession, geometry: toolbar_layout.SelectionRect) void {
        self.selection_rect = geometry;
        const badge = badgeRect(geometry);
        const result = toolbar_layout.compute(
            self.output,
            geometry,
            self.toolbar.size,
            badge,
            self.toolbar.position,
            .{},
        );
        self.toolbar.position = result.position;
        self.toolbar.placement = result.placement;
    }
};

pub fn defaultOutput() toolbar_layout.Rect {
    return .{ .x = 0, .y = 0, .width = 800, .height = 600 };
}

fn badgeRect(geometry: toolbar_layout.SelectionRect) toolbar_layout.Rect {
    const text_width: u32 = if (geometry.width >= 1000 or geometry.height >= 1000) 116 else 96;
    const height: u32 = 24;
    return .{
        .x = geometry.x,
        .y = geometry.y - 30,
        .width = text_width,
        .height = height,
    };
}

test "all-in-one session reuses persisted toolbar before selection" {
    const session = AllInOneSession.init(defaultOutput(), .{ .x = 100, .y = 500 });
    try std.testing.expect(session.toolbar.position != null);
    try std.testing.expectEqual(@as(i32, 100), session.toolbar.position.?.x);
    try std.testing.expect(session.selection_rect == null);
}

test "all-in-one session updates toolbar from active selection" {
    var session = AllInOneSession.init(defaultOutput(), null);
    session.updateSelection(.{ .x = 100, .y = 100, .width = 400, .height = 300 });
    try std.testing.expect(session.toolbar.position != null);
    try std.testing.expectEqual(toolbar_layout.Placement.below, session.toolbar.placement);
}
