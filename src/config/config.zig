const std = @import("std");
const core_capture_mode = @import("../core/capture_mode.zig");

pub const preview_app_id = "dev.shaula.preview";
pub const preview_title = "Shaula Preview";

pub const Config = struct {
    capture: CaptureConfig = .{},
    preview: PreviewConfig = .{},
};

pub const CaptureConfig = struct {
    region_capture_mode: core_capture_mode.RegionCaptureMode = .live,
};

pub const PreviewConfig = struct {
    window: PreviewWindowConfig = .{},
};

pub const PreviewWindowMode = enum {
    auto,
    tiling,
    floating,
    maximized,
    maximized_to_edges,
    fullscreen,

    pub fn asString(mode: PreviewWindowMode) []const u8 {
        return switch (mode) {
            .auto => "auto",
            .tiling => "tiling",
            .floating => "floating",
            .maximized => "maximized",
            .maximized_to_edges => "maximized-to-edges",
            .fullscreen => "fullscreen",
        };
    }
};

pub const ColumnDisplay = enum {
    normal,
    tabbed,

    pub fn asString(display: ColumnDisplay) []const u8 {
        return switch (display) {
            .normal => "normal",
            .tabbed => "tabbed",
        };
    }
};

pub const FloatingRelativeTo = enum {
    top_left,
    top_right,
    bottom_left,
    bottom_right,
    center,

    pub fn asString(relative_to: FloatingRelativeTo) []const u8 {
        return switch (relative_to) {
            .top_left => "top-left",
            .top_right => "top-right",
            .bottom_left => "bottom-left",
            .bottom_right => "bottom-right",
            .center => "center",
        };
    }
};

pub const PreviewWindowConfig = struct {
    mode: PreviewWindowMode = .floating,
    focused: bool = true,
    width: ?u32 = 1100,
    height: ?u32 = 720,
    default_column_display: ColumnDisplay = .normal,
    floating_position: FloatingPositionConfig = .{},
};

pub const FloatingPositionConfig = struct {
    x: ?i32 = null,
    y: ?i32 = null,
    relative_to: FloatingRelativeTo = .top_left,
};

pub fn parsePreviewWindowMode(value: []const u8) ?PreviewWindowMode {
    if (std.mem.eql(u8, value, "auto")) return .auto;
    if (std.mem.eql(u8, value, "tiling")) return .tiling;
    if (std.mem.eql(u8, value, "floating")) return .floating;
    if (std.mem.eql(u8, value, "maximized")) return .maximized;
    if (std.mem.eql(u8, value, "maximized-to-edges")) return .maximized_to_edges;
    if (std.mem.eql(u8, value, "fullscreen")) return .fullscreen;
    return null;
}

pub fn parseRegionCaptureMode(value: []const u8) ?core_capture_mode.RegionCaptureMode {
    return core_capture_mode.parseRegionCaptureMode(value);
}

pub fn parseColumnDisplay(value: []const u8) ?ColumnDisplay {
    if (std.mem.eql(u8, value, "normal")) return .normal;
    if (std.mem.eql(u8, value, "tabbed")) return .tabbed;
    return null;
}

pub fn parseFloatingRelativeTo(value: []const u8) ?FloatingRelativeTo {
    if (std.mem.eql(u8, value, "top-left")) return .top_left;
    if (std.mem.eql(u8, value, "top-right")) return .top_right;
    if (std.mem.eql(u8, value, "bottom-left")) return .bottom_left;
    if (std.mem.eql(u8, value, "bottom-right")) return .bottom_right;
    if (std.mem.eql(u8, value, "center")) return .center;
    return null;
}
