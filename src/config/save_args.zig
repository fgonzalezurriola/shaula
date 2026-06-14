const std = @import("std");

const config_types = @import("config.zig");

pub const ApplyResult = union(enum) {
    not_save_setting,
    applied: usize,
    invalid: Invalid,
};

pub const Invalid = struct {
    message: []const u8,
    field: ?[]const u8 = null,
};

/// Apply one `config save` setting flag to the in-memory config draft.
///
/// Contract constraints: this parser only owns setting flags. Command-level
/// flags such as `--json`, `--force`, `--dry-run`, and `--apply-niri` stay in
/// the caller so their existing `ERR_CLI_USAGE` behavior remains explicit.
pub fn apply(
    config: *config_types.Config,
    subcommand: []const u8,
    arg: []const u8,
    next: ?[]const u8,
) ApplyResult {
    if (std.mem.eql(u8, arg, "--region-mode")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--region-mode is supported only for save and requires a value" } };
        config.capture.region_capture_mode = config_types.parseRegionCaptureMode(value) orelse return .{ .invalid = .{ .message = "invalid --region-mode", .field = "region_mode" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--preview-mode")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--preview-mode is supported only for save and requires a value" } };
        config.preview.window.mode = config_types.parsePreviewWindowMode(value) orelse return .{ .invalid = .{ .message = "invalid --preview-mode", .field = "preview_mode" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--focused")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--focused is supported only for save and requires a value" } };
        config.preview.window.focused = parseBoolArg(value) orelse return .{ .invalid = .{ .message = "invalid --focused", .field = "focused" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--close-preview-on-save")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--close-preview-on-save is supported only for save and requires a value" } };
        config.preview.window.close_preview_on_save = parseBoolArg(value) orelse return .{ .invalid = .{ .message = "invalid --close-preview-on-save", .field = "close_preview_on_save" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--width")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--width is supported only for save and requires a value" } };
        config.preview.window.width = parsePositiveU32Arg(value) orelse return .{ .invalid = .{ .message = "invalid --width", .field = "width" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--height")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--height is supported only for save and requires a value" } };
        config.preview.window.height = parsePositiveU32Arg(value) orelse return .{ .invalid = .{ .message = "invalid --height", .field = "height" } };
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--floating-position")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--floating-position is supported only for save and requires a value" } };
        if (std.mem.eql(u8, value, "centered")) {
            config.preview.window.floating_position.x = null;
            config.preview.window.floating_position.y = null;
            config.preview.window.floating_position.relative_to = .top_left;
        } else if (std.mem.eql(u8, value, "top-left")) {
            config.preview.window.floating_position.x = 80;
            config.preview.window.floating_position.y = 80;
            config.preview.window.floating_position.relative_to = .top_left;
        } else if (std.mem.eql(u8, value, "top-right")) {
            config.preview.window.floating_position.x = 80;
            config.preview.window.floating_position.y = 80;
            config.preview.window.floating_position.relative_to = .top_right;
        } else {
            return .{ .invalid = .{ .message = "invalid --floating-position", .field = "floating_position" } };
        }
        return .{ .applied = 1 };
    }
    if (std.mem.eql(u8, arg, "--save-folder")) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "--save-folder is supported only for save and requires a value" } };
        if (!validSaveFolderArg(value)) return .{ .invalid = .{ .message = "invalid --save-folder", .field = "save_folder" } };
        config.capture.after.save_folder.set(value) catch return .{ .invalid = .{ .message = "invalid --save-folder", .field = "save_folder" } };
        return .{ .applied = 1 };
    }
    if (isBooleanSetting(arg)) {
        const value = saveValue(subcommand, next) orelse return .{ .invalid = .{ .message = "after-capture setting requires a boolean value" } };
        const parsed = parseBoolArg(value) orelse return .{ .invalid = .{ .message = "invalid after-capture boolean", .field = arg } };
        applyBoolSetting(config, arg, parsed);
        return .{ .applied = 1 };
    }
    return .not_save_setting;
}

fn saveValue(subcommand: []const u8, next: ?[]const u8) ?[]const u8 {
    if (!std.mem.eql(u8, subcommand, "save")) return null;
    return next;
}

fn isBooleanSetting(arg: []const u8) bool {
    return std.mem.eql(u8, arg, "--after-quick-skip-preview") or
        std.mem.eql(u8, arg, "--after-area-skip-preview") or
        std.mem.eql(u8, arg, "--after-fullscreen-skip-preview") or
        std.mem.eql(u8, arg, "--after-all-screens-skip-preview") or
        std.mem.eql(u8, arg, "--after-quick-copy") or
        std.mem.eql(u8, arg, "--after-area-copy") or
        std.mem.eql(u8, arg, "--after-fullscreen-copy") or
        std.mem.eql(u8, arg, "--after-all-screens-copy") or
        std.mem.eql(u8, arg, "--after-quick-save") or
        std.mem.eql(u8, arg, "--after-area-save") or
        std.mem.eql(u8, arg, "--after-fullscreen-save") or
        std.mem.eql(u8, arg, "--after-all-screens-save") or
        std.mem.eql(u8, arg, "--notifications-success") or
        std.mem.eql(u8, arg, "--notifications-errors") or
        std.mem.eql(u8, arg, "--notifications-thumbnails");
}

fn applyBoolSetting(config: *config_types.Config, arg: []const u8, value: bool) void {
    if (std.mem.eql(u8, arg, "--after-quick-skip-preview")) config.capture.after.quick.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-area-skip-preview")) config.capture.after.area.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-skip-preview")) config.capture.after.fullscreen.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-skip-preview")) config.capture.after.all_screens.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-quick-copy")) config.capture.after.quick.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-area-copy")) config.capture.after.area.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-copy")) config.capture.after.fullscreen.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-copy")) config.capture.after.all_screens.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-quick-save")) config.capture.after.quick.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-area-save")) config.capture.after.area.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-save")) config.capture.after.fullscreen.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-save")) config.capture.after.all_screens.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--notifications-success")) config.notifications.success = value;
    if (std.mem.eql(u8, arg, "--notifications-errors")) config.notifications.errors = value;
    if (std.mem.eql(u8, arg, "--notifications-thumbnails")) config.notifications.thumbnails = value;
}

fn validSaveFolderArg(value: []const u8) bool {
    if (value.len == 0) return true;
    if (std.mem.indexOfAny(u8, value, "\"\\") != null) return false;
    if (std.mem.eql(u8, value, "~")) return true;
    if (std.mem.startsWith(u8, value, "~/")) return true;
    return std.fs.path.isAbsolute(value);
}

fn parseBoolArg(value: []const u8) ?bool {
    if (std.mem.eql(u8, value, "true")) return true;
    if (std.mem.eql(u8, value, "false")) return false;
    return null;
}

fn parsePositiveU32Arg(value: []const u8) ?u32 {
    if (std.mem.startsWith(u8, value, "-")) return null;
    const parsed = std.fmt.parseInt(u32, value, 10) catch return null;
    if (parsed == 0) return null;
    return parsed;
}
