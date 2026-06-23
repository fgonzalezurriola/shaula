const std = @import("std");
const core_capture_mode = @import("../core/capture_mode.zig");

pub const preview_app_id = "dev.shaula.preview";
pub const preview_title = "Shaula Preview";

pub const Config = struct {
    capture: CaptureConfig = .{},
    preview: PreviewConfig = .{},
    notifications: NotificationsConfig = .{},
};

pub const CaptureConfig = struct {
    region_capture_mode: core_capture_mode.RegionCaptureMode = .frozen,
    after: CaptureAfterConfig = .{},
};

pub const CaptureAfterConfig = struct {
    quick: CaptureAfterModeConfig = .{ .copy_to_clipboard = true },
    area: CaptureAfterModeConfig = .{ .copy_to_clipboard = true },
    fullscreen: CaptureAfterModeConfig = .{ .skip_preview = true, .copy_to_clipboard = true, .save_to_folder = true },
    all_screens: CaptureAfterModeConfig = .{ .skip_preview = true, .copy_to_clipboard = true, .save_to_folder = true },
    save_folder: SaveFolderConfig = .{},
};

pub const CaptureAfterModeConfig = struct {
    skip_preview: bool = false,
    copy_to_clipboard: bool = false,
    save_to_folder: bool = false,
};

pub const default_save_folder = "~/Pictures/shaula";

pub const SaveFolderConfig = struct {
    bytes: [4096]u8 = undefined,
    len: usize = 0,

    pub fn value(self: *const SaveFolderConfig) []const u8 {
        return if (self.len == 0) default_save_folder else self.bytes[0..self.len];
    }

    pub fn set(self: *SaveFolderConfig, value_text: []const u8) !void {
        if (value_text.len > self.bytes.len) return error.SaveFolderTooLong;
        @memcpy(self.bytes[0..value_text.len], value_text);
        self.len = value_text.len;
    }
};

pub const NotificationsConfig = struct {
    success: bool = true,
    errors: bool = true,
    thumbnails: bool = true,
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
    close_preview_on_save: bool = true,
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

pub fn validateCaptureAfter(config: CaptureAfterConfig) bool {
    return validateCaptureAfterMode(config.quick) and
        validateCaptureAfterMode(config.area) and
        validateCaptureAfterMode(config.fullscreen) and
        validateCaptureAfterMode(config.all_screens);
}

pub fn validateCaptureAfterMode(mode: CaptureAfterModeConfig) bool {
    if (!mode.skip_preview) return true;
    return mode.copy_to_clipboard or mode.save_to_folder;
}
