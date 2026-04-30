const std = @import("std");

const config_types = @import("config.zig");

pub const RenderedRule = struct {
    kdl: []u8,
    warnings: []const []const u8,

    pub fn deinit(self: *RenderedRule, allocator: std.mem.Allocator) void {
        allocator.free(self.kdl);
    }
};

/// Render the Niri window-rule that makes the preview window follow Shaula's
/// user configuration while leaving Niri's own config under user control.
pub fn renderPreviewWindowRule(allocator: std.mem.Allocator, config: config_types.Config) !RenderedRule {
    var out = std.ArrayList(u8).empty;
    errdefer out.deinit(allocator);

    const window = config.preview.window;
    try out.appendSlice(allocator, "window-rule {\n");
    try out.appendSlice(allocator, "    match app-id=\"^dev\\\\.shaula\\\\.preview$\"\n");

    switch (window.mode) {
        .auto => {},
        .tiling => try out.appendSlice(allocator, "    open-floating false\n"),
        .floating => try out.appendSlice(allocator, "    open-floating true\n"),
        .maximized => {
            try out.appendSlice(allocator, "    open-floating false\n");
            try out.appendSlice(allocator, "    open-maximized true\n");
        },
        .maximized_to_edges => {
            try out.appendSlice(allocator, "    open-floating false\n");
            try out.appendSlice(allocator, "    open-maximized-to-edges true\n");
        },
        .fullscreen => {
            try out.appendSlice(allocator, "    open-floating false\n");
            try out.appendSlice(allocator, "    open-fullscreen true\n");
        },
    }

    try out.print(allocator, "    open-focused {s}\n", .{if (window.focused) "true" else "false"});
    if (window.width) |width| {
        try out.print(allocator, "    default-column-width {{ fixed {d}; }}\n", .{width});
    }
    if (window.height) |height| {
        try out.print(allocator, "    default-window-height {{ fixed {d}; }}\n", .{height});
    }
    try out.print(allocator, "    default-column-display \"{s}\"\n", .{window.default_column_display.asString()});

    var floating_position_ignored = false;
    const floating_position = window.floating_position;
    if (floating_position.x != null or floating_position.y != null) {
        if (window.mode == .floating and floating_position.x != null and floating_position.y != null) {
            try out.print(
                allocator,
                "    default-floating-position x={d} y={d} relative-to=\"{s}\"\n",
                .{ floating_position.x.?, floating_position.y.?, floating_position.relative_to.asString() },
            );
        } else {
            floating_position_ignored = true;
        }
    }

    try out.appendSlice(allocator, "}\n");
    return .{
        .kdl = try out.toOwnedSlice(allocator),
        .warnings = if (floating_position_ignored) &.{"preview_window_floating_position_ignored_for_mode"} else &.{},
    };
}

test "niri rule renders floating mode" {
    var rendered = try renderPreviewWindowRule(std.testing.allocator, .{});
    defer rendered.deinit(std.testing.allocator);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "open-floating true") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "default-column-width { fixed 1100; }") != null);
}

test "niri rule renders maximized to edges" {
    var cfg: config_types.Config = .{};
    cfg.preview.window.mode = .maximized_to_edges;
    var rendered = try renderPreviewWindowRule(std.testing.allocator, cfg);
    defer rendered.deinit(std.testing.allocator);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "open-floating false") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "open-maximized-to-edges true") != null);
}

test "niri rule ignores floating position outside floating mode" {
    var cfg: config_types.Config = .{};
    cfg.preview.window.mode = .tiling;
    cfg.preview.window.floating_position.x = 80;
    cfg.preview.window.floating_position.y = 80;
    var rendered = try renderPreviewWindowRule(std.testing.allocator, cfg);
    defer rendered.deinit(std.testing.allocator);
    try std.testing.expectEqual(@as(usize, 1), rendered.warnings.len);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "default-floating-position") == null);
}
