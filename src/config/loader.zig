const std = @import("std");

const config_types = @import("config.zig");

pub const ConfigLoadResult = struct {
    path: ?[]u8,
    loaded: bool,
    config: config_types.Config,

    pub fn deinit(self: *ConfigLoadResult, allocator: std.mem.Allocator) void {
        if (self.path) |path| allocator.free(path);
        self.path = null;
    }
};

const Section = enum {
    root,
    capture,
    capture_after,
    capture_after_quick,
    capture_after_area,
    capture_after_fullscreen,
    capture_after_all_screens,
    notifications,
    preview_window,
    preview_window_floating_position,
};

const ConfigParseError = error{
    ConfigInvalid,
    ConfigUnreadable,
};

const EnvPair = struct {
    key: []const u8,
    value: []const u8,
};

const TestEnviron = struct {
    environ: std.process.Environ,
    block: std.process.Environ.Block,

    fn deinit(self: *TestEnviron, allocator: std.mem.Allocator) void {
        self.block.deinit(allocator);
    }
};

/// Resolve Shaula's user config path without creating files or directories.
///
/// Contract constraints:
/// - `SHAULA_CONFIG_FILE` wins when non-empty.
/// - `XDG_CONFIG_HOME` wins over `HOME`.
/// - missing config path is allowed and returns `null`.
pub fn resolveConfigPath(allocator: std.mem.Allocator, environ: std.process.Environ) !?[]u8 {
    if (environ.getPosix("SHAULA_CONFIG_FILE")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return try allocator.dupe(u8, raw);
    }

    if (environ.getPosix("XDG_CONFIG_HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return try std.fmt.allocPrint(allocator, "{s}/shaula/config.toml", .{raw});
    }

    if (environ.getPosix("HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return try std.fmt.allocPrint(allocator, "{s}/.config/shaula/config.toml", .{raw});
    }

    return null;
}

pub fn load(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !ConfigLoadResult {
    const path = try resolveConfigPath(allocator, environ);
    errdefer if (path) |resolved| allocator.free(resolved);

    if (path == null) {
        return .{ .path = null, .loaded = false, .config = .{} };
    }

    std.Io.Dir.cwd().access(io, path.?, .{}) catch |err| switch (err) {
        error.FileNotFound => return .{ .path = path, .loaded = false, .config = .{} },
        else => return error.ConfigUnreadable,
    };

    const contents = std.Io.Dir.cwd().readFileAlloc(io, path.?, allocator, .limited(64 * 1024)) catch return error.ConfigUnreadable;
    defer allocator.free(contents);

    const parsed = parseTomlSubset(allocator, contents) catch return error.ConfigInvalid;
    return .{ .path = path, .loaded = true, .config = parsed };
}

/// Parse Shaula's initial TOML subset.
///
/// This parser intentionally accepts only the public configuration contract so
/// misspelled fields fail deterministically as `ERR_CONFIG_INVALID`.
pub fn parseTomlSubset(allocator: std.mem.Allocator, bytes: []const u8) !config_types.Config {
    _ = allocator;
    var parsed: config_types.Config = .{};
    var section: Section = .root;

    var lines = std.mem.splitScalar(u8, bytes, '\n');
    while (lines.next()) |raw_line| {
        const no_comment = stripComment(raw_line);
        const line = std.mem.trim(u8, no_comment, " \t\r");
        if (line.len == 0) continue;

        if (line[0] == '[') {
            if (line[line.len - 1] != ']') return error.ConfigInvalid;
            const name = std.mem.trim(u8, line[1 .. line.len - 1], " \t");
            if (std.mem.eql(u8, name, "capture")) {
                section = .capture;
            } else if (std.mem.eql(u8, name, "capture.after")) {
                section = .capture_after;
            } else if (std.mem.eql(u8, name, "capture.after.quick")) {
                section = .capture_after_quick;
            } else if (std.mem.eql(u8, name, "capture.after.area")) {
                section = .capture_after_area;
            } else if (std.mem.eql(u8, name, "capture.after.fullscreen")) {
                section = .capture_after_fullscreen;
            } else if (std.mem.eql(u8, name, "capture.after.all_screens")) {
                section = .capture_after_all_screens;
            } else if (std.mem.eql(u8, name, "notifications")) {
                section = .notifications;
            } else if (std.mem.eql(u8, name, "preview.window")) {
                section = .preview_window;
            } else if (std.mem.eql(u8, name, "preview.window.floating_position")) {
                section = .preview_window_floating_position;
            } else {
                return error.ConfigInvalid;
            }
            continue;
        }

        const eq_index = std.mem.indexOfScalar(u8, line, '=') orelse return error.ConfigInvalid;
        const key = std.mem.trim(u8, line[0..eq_index], " \t");
        const value = std.mem.trim(u8, line[eq_index + 1 ..], " \t");
        if (key.len == 0 or value.len == 0) return error.ConfigInvalid;

        switch (section) {
            .root => return error.ConfigInvalid,
            .capture => try parseCaptureField(&parsed, key, value),
            .capture_after => try parseCaptureAfterField(&parsed, key, value),
            .capture_after_quick => try parseCaptureAfterModeField(&parsed.capture.after.quick, key, value),
            .capture_after_area => try parseCaptureAfterModeField(&parsed.capture.after.area, key, value),
            .capture_after_fullscreen => try parseCaptureAfterModeField(&parsed.capture.after.fullscreen, key, value),
            .capture_after_all_screens => try parseCaptureAfterModeField(&parsed.capture.after.all_screens, key, value),
            .notifications => try parseNotificationsField(&parsed, key, value),
            .preview_window => try parsePreviewWindowField(&parsed, key, value),
            .preview_window_floating_position => try parseFloatingPositionField(&parsed, key, value),
        }
    }

    if (!config_types.validateCaptureAfter(parsed.capture.after)) return error.ConfigInvalid;
    return parsed;
}

fn stripComment(line: []const u8) []const u8 {
    var in_string = false;
    var escaped = false;
    for (line, 0..) |ch, index| {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' and in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (ch == '#' and !in_string) return line[0..index];
    }
    return line;
}

fn parseCaptureField(parsed: *config_types.Config, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "region_capture_mode")) {
        const text = try parseString(value);
        parsed.capture.region_capture_mode = config_types.parseRegionCaptureMode(text) orelse return error.ConfigInvalid;
        return;
    }
    return error.ConfigInvalid;
}

fn parseCaptureAfterField(parsed: *config_types.Config, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "save_folder")) {
        const text = try parseString(value);
        if (!validSaveFolder(text)) return error.ConfigInvalid;
        parsed.capture.after.save_folder.set(text) catch return error.ConfigInvalid;
        return;
    }
    return error.ConfigInvalid;
}

fn parseCaptureAfterModeField(mode: *config_types.CaptureAfterModeConfig, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "skip_preview")) {
        mode.skip_preview = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "copy_to_clipboard")) {
        mode.copy_to_clipboard = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "save_to_folder")) {
        mode.save_to_folder = try parseBool(value);
        return;
    }
    return error.ConfigInvalid;
}

fn parseNotificationsField(parsed: *config_types.Config, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "success")) {
        parsed.notifications.success = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "errors")) {
        parsed.notifications.errors = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "thumbnails")) {
        parsed.notifications.thumbnails = try parseBool(value);
        return;
    }
    return error.ConfigInvalid;
}

fn validSaveFolder(value: []const u8) bool {
    if (value.len == 0) return true;
    if (std.mem.indexOfAny(u8, value, "\"\\") != null) return false;
    if (std.mem.eql(u8, value, "~")) return true;
    if (std.mem.startsWith(u8, value, "~/")) return true;
    return std.fs.path.isAbsolute(value);
}

fn parsePreviewWindowField(parsed: *config_types.Config, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "mode")) {
        const text = try parseString(value);
        parsed.preview.window.mode = config_types.parsePreviewWindowMode(text) orelse return error.ConfigInvalid;
        return;
    }
    if (std.mem.eql(u8, key, "focused")) {
        parsed.preview.window.focused = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "close_preview_on_save")) {
        parsed.preview.window.close_preview_on_save = try parseBool(value);
        return;
    }
    if (std.mem.eql(u8, key, "width")) {
        parsed.preview.window.width = try parsePositiveU32(value);
        return;
    }
    if (std.mem.eql(u8, key, "height")) {
        parsed.preview.window.height = try parsePositiveU32(value);
        return;
    }
    if (std.mem.eql(u8, key, "default_column_display")) {
        const text = try parseString(value);
        parsed.preview.window.default_column_display = config_types.parseColumnDisplay(text) orelse return error.ConfigInvalid;
        return;
    }
    return error.ConfigInvalid;
}

fn parseFloatingPositionField(parsed: *config_types.Config, key: []const u8, value: []const u8) !void {
    if (std.mem.eql(u8, key, "x")) {
        parsed.preview.window.floating_position.x = try parseI32(value);
        return;
    }
    if (std.mem.eql(u8, key, "y")) {
        parsed.preview.window.floating_position.y = try parseI32(value);
        return;
    }
    if (std.mem.eql(u8, key, "relative_to")) {
        const text = try parseString(value);
        parsed.preview.window.floating_position.relative_to = config_types.parseFloatingRelativeTo(text) orelse return error.ConfigInvalid;
        return;
    }
    return error.ConfigInvalid;
}

fn parseString(value: []const u8) ![]const u8 {
    if (value.len < 2 or value[0] != '"' or value[value.len - 1] != '"') return error.ConfigInvalid;
    const inner = value[1 .. value.len - 1];
    if (std.mem.indexOfScalar(u8, inner, '"') != null) return error.ConfigInvalid;
    return inner;
}

fn parseBool(value: []const u8) !bool {
    if (std.mem.eql(u8, value, "true")) return true;
    if (std.mem.eql(u8, value, "false")) return false;
    return error.ConfigInvalid;
}

fn parsePositiveU32(value: []const u8) !u32 {
    if (std.mem.startsWith(u8, value, "-")) return error.ConfigInvalid;
    const parsed = std.fmt.parseInt(u32, value, 10) catch return error.ConfigInvalid;
    if (parsed == 0) return error.ConfigInvalid;
    return parsed;
}

fn parseI32(value: []const u8) !i32 {
    return std.fmt.parseInt(i32, value, 10) catch return error.ConfigInvalid;
}

test "config path prefers explicit env" {
    var test_env = try initTestEnviron(&.{
        .{ .key = "SHAULA_CONFIG_FILE", .value = "/tmp/shaula.toml" },
        .{ .key = "XDG_CONFIG_HOME", .value = "/tmp/xdg" },
    });
    defer test_env.deinit(std.testing.allocator);
    const path = (try resolveConfigPath(std.testing.allocator, test_env.environ)).?;
    defer std.testing.allocator.free(path);
    try std.testing.expectEqualStrings("/tmp/shaula.toml", path);
}

test "config path prefers XDG over HOME" {
    var test_env = try initTestEnviron(&.{
        .{ .key = "XDG_CONFIG_HOME", .value = "/tmp/xdg" },
        .{ .key = "HOME", .value = "/tmp/home" },
    });
    defer test_env.deinit(std.testing.allocator);
    const path = (try resolveConfigPath(std.testing.allocator, test_env.environ)).?;
    defer std.testing.allocator.free(path);
    try std.testing.expectEqualStrings("/tmp/xdg/shaula/config.toml", path);
}

test "config path falls back to HOME" {
    var test_env = try initTestEnviron(&.{
        .{ .key = "HOME", .value = "/tmp/home" },
    });
    defer test_env.deinit(std.testing.allocator);
    const path = (try resolveConfigPath(std.testing.allocator, test_env.environ)).?;
    defer std.testing.allocator.free(path);
    try std.testing.expectEqualStrings("/tmp/home/.config/shaula/config.toml", path);
}

fn initTestEnviron(pairs: []const EnvPair) !TestEnviron {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    for (pairs) |pair| {
        try map.put(pair.key, pair.value);
    }

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    return .{
        .environ = .{ .block = block },
        .block = block,
    };
}

test "config parser keeps defaults for empty file" {
    const parsed = try parseTomlSubset(std.testing.allocator, "");
    try std.testing.expectEqual(config_types.PreviewWindowMode.floating, parsed.preview.window.mode);
    try std.testing.expectEqual(@as(?u32, 1100), parsed.preview.window.width);
}

test "config parser accepts preview window fields" {
    const parsed = try parseTomlSubset(std.testing.allocator,
        \\[preview.window]
        \\mode = "maximized-to-edges"
        \\focused = false
        \\width = 900
        \\height = 600
        \\default_column_display = "tabbed"
        \\
        \\[preview.window.floating_position]
        \\x = 80
        \\y = -40
        \\relative_to = "top-right"
    );
    try std.testing.expectEqual(config_types.PreviewWindowMode.maximized_to_edges, parsed.preview.window.mode);
    try std.testing.expect(!parsed.preview.window.focused);
    try std.testing.expectEqual(@as(?u32, 900), parsed.preview.window.width);
    try std.testing.expectEqual(@as(?i32, -40), parsed.preview.window.floating_position.y);
    try std.testing.expectEqual(config_types.FloatingRelativeTo.top_right, parsed.preview.window.floating_position.relative_to);
}

test "config parser rejects unknown keys" {
    try std.testing.expectError(error.ConfigInvalid, parseTomlSubset(std.testing.allocator,
        \\[preview.window]
        \\heigth = 600
    ));
}

test "config parser rejects invalid enum and non-positive size" {
    try std.testing.expectError(error.ConfigInvalid, parseTomlSubset(std.testing.allocator,
        \\[preview.window]
        \\mode = "normal"
    ));
    try std.testing.expectError(error.ConfigInvalid, parseTomlSubset(std.testing.allocator,
        \\[preview.window]
        \\width = 0
    ));
}
