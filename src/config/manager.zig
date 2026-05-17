const std = @import("std");

const loader = @import("loader.zig");
const config_types = @import("config.zig");
const niri_keybinds = @import("niri_keybinds.zig");

pub const default_config_toml =
    \\[capture]
    \\# live keeps the desktop updating while selecting. frozen shows a still
    \\# screen while selecting for transient states.
    \\region_capture_mode = "frozen"
    \\
    \\[capture.after]
    \\save_folder = "~/Pictures/shaula"
    \\
    \\[capture.after.quick]
    \\skip_preview = false
    \\copy_to_clipboard = true
    \\save_to_folder = false
    \\
    \\[capture.after.area]
    \\skip_preview = false
    \\copy_to_clipboard = true
    \\save_to_folder = false
    \\
    \\[capture.after.fullscreen]
    \\skip_preview = true
    \\copy_to_clipboard = true
    \\save_to_folder = false
    \\
    \\[capture.after.all_screens]
    \\skip_preview = true
    \\copy_to_clipboard = true
    \\save_to_folder = false
    \\
    \\[notifications]
    \\success = true
    \\errors = true
    \\thumbnails = true
    \\
    \\[preview.window]
    \\mode = "floating"
    \\focused = true
    \\close_preview_on_save = true
    \\width = 1100
    \\height = 720
    \\default_column_display = "normal"
    \\
    \\[preview.window.floating_position]
    \\# x and y are optional. When both are set, Shaula's generated Niri rule
    \\# includes default-floating-position.
    \\# x = 80
    \\# y = 80
    \\relative_to = "top-left"
    \\
;

pub const managed_block_begin = "// BEGIN SHAULA PREVIEW WINDOW RULE";
pub const managed_block_end = "// END SHAULA PREVIEW WINDOW RULE";
const max_backup_attempts = 100;

pub const InitOptions = struct {
    overwrite: bool = false,
    dry_run: bool = false,
};

pub const InitResult = struct {
    path: []u8,
    created: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *InitResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
    }
};

pub const InstallOptions = struct {
    path_override: ?[]const u8 = null,
    dry_run: bool = false,
};

pub const InstallResult = struct {
    path: []u8,
    backup_path: ?[]u8,
    installed: bool,
    replaced: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *InstallResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
        if (self.backup_path) |path| allocator.free(path);
    }
};

pub const SaveOptions = struct {
    config: config_types.Config,
    dry_run: bool = false,
    force_canonical: bool = false,
};

pub const SaveResult = struct {
    path: []u8,
    backup_path: ?[]u8,
    created: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *SaveResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
        if (self.backup_path) |path| allocator.free(path);
    }
};

pub fn defaultConfigPath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    const resolved = try loader.resolveConfigPath(allocator, environ);
    if (resolved) |path| return path;
    return error.ConfigUnreadable;
}

/// Create Shaula's config file using atomic directory creation and explicit
/// overwrite semantics so the same operation can be safely exposed by UI later.
pub fn initConfig(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: InitOptions) !InitResult {
    const path = try defaultConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const exists = pathExists(io, path);
    if (exists and !options.overwrite) {
        return .{
            .path = path,
            .created = false,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    if (!options.dry_run) {
        try ensureParentDir(io, path);
        try writeFileAtomic(io, path, default_config_toml);
    }

    return .{
        .path = path,
        .created = !exists,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

/// Save Shaula's public config contract while preserving valid user comments.
///
/// Contract constraints:
/// - existing config must already parse through `loader.load`, so unsupported
///   fields still fail deterministically as `ERR_CONFIG_INVALID` before save.
/// - valid existing files are patched by section/key to preserve comments and
///   ordering where practical.
/// - new/reset files use the canonical documented config shape.
/// - changed existing files are backed up before atomic replacement.
pub fn saveConfig(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SaveOptions) !SaveResult {
    const path = try defaultConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const exists = pathExists(io, path);
    const current = if (exists)
        try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(64 * 1024))
    else
        try allocator.dupe(u8, "");
    defer allocator.free(current);

    if (exists and !options.force_canonical) {
        _ = loader.parseTomlSubset(allocator, current) catch return error.ConfigInvalid;
    }

    const next = if (exists and !options.force_canonical)
        try patchConfigText(allocator, current, options.config)
    else
        try canonicalConfigText(allocator, options.config);
    defer allocator.free(next);

    if (std.mem.eql(u8, current, next)) {
        return .{
            .path = path,
            .backup_path = null,
            .created = false,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    var backup_path: ?[]u8 = null;
    if (!options.dry_run) {
        try ensureParentDir(io, path);
        if (exists and current.len > 0) {
            backup_path = try backupExisting(allocator, io, path, current);
        }
        try writeFileAtomic(io, path, next);
    }

    return .{
        .path = path,
        .backup_path = backup_path,
        .created = !exists,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

fn canonicalConfigText(allocator: std.mem.Allocator, config: config_types.Config) ![]u8 {
    const window = config.preview.window;
    const floating = window.floating_position;
    const floating_xy = if (floating.x != null and floating.y != null)
        try std.fmt.allocPrint(allocator, "x = {d}\ny = {d}\n", .{ floating.x.?, floating.y.? })
    else
        try allocator.dupe(u8, "# x = 80\n# y = 80\n");
    defer allocator.free(floating_xy);

    return std.fmt.allocPrint(allocator,
        \\[capture]
        \\# live keeps the desktop updating while selecting. frozen shows a still
        \\# screen while selecting for transient states.
        \\region_capture_mode = "{s}"
        \\
        \\[capture.after]
        \\save_folder = "{s}"
        \\
        \\[capture.after.quick]
        \\skip_preview = {s}
        \\copy_to_clipboard = {s}
        \\save_to_folder = {s}
        \\
        \\[capture.after.area]
        \\skip_preview = {s}
        \\copy_to_clipboard = {s}
        \\save_to_folder = {s}
        \\
        \\[capture.after.fullscreen]
        \\skip_preview = {s}
        \\copy_to_clipboard = {s}
        \\save_to_folder = {s}
        \\
        \\[capture.after.all_screens]
        \\skip_preview = {s}
        \\copy_to_clipboard = {s}
        \\save_to_folder = {s}
        \\
        \\[notifications]
        \\success = {s}
        \\errors = {s}
        \\thumbnails = {s}
        \\
        \\[preview.window]
        \\mode = "{s}"
        \\focused = {s}
        \\close_preview_on_save = {s}
        \\width = {d}
        \\height = {d}
        \\default_column_display = "{s}"
        \\
        \\[preview.window.floating_position]
        \\# x and y are optional. When both are set, Shaula's generated Niri rule
        \\# includes default-floating-position.
        \\{s}relative_to = "{s}"
        \\
    , .{
        config.capture.region_capture_mode.asString(),
        config.capture.after.save_folder.value(),
        boolText(config.capture.after.quick.skip_preview),
        boolText(config.capture.after.quick.copy_to_clipboard),
        boolText(config.capture.after.quick.save_to_folder),
        boolText(config.capture.after.area.skip_preview),
        boolText(config.capture.after.area.copy_to_clipboard),
        boolText(config.capture.after.area.save_to_folder),
        boolText(config.capture.after.fullscreen.skip_preview),
        boolText(config.capture.after.fullscreen.copy_to_clipboard),
        boolText(config.capture.after.fullscreen.save_to_folder),
        boolText(config.capture.after.all_screens.skip_preview),
        boolText(config.capture.after.all_screens.copy_to_clipboard),
        boolText(config.capture.after.all_screens.save_to_folder),
        boolText(config.notifications.success),
        boolText(config.notifications.errors),
        boolText(config.notifications.thumbnails),
        window.mode.asString(),
        if (window.focused) "true" else "false",
        if (window.close_preview_on_save) "true" else "false",
        window.width orelse 1100,
        window.height orelse 720,
        window.default_column_display.asString(),
        floating_xy,
        floating.relative_to.asString(),
    });
}

const ConfigSection = enum {
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

const SeenConfigFields = struct {
    region_mode: bool = false,
    save_folder: bool = false,
    after_skip_preview: bool = false,
    after_copy: bool = false,
    after_save: bool = false,
    notifications_success: bool = false,
    notifications_errors: bool = false,
    notifications_thumbnails: bool = false,
    preview_mode: bool = false,
    focused: bool = false,
    close_preview_on_save: bool = false,
    width: bool = false,
    height: bool = false,
    column_display: bool = false,
    floating_x: bool = false,
    floating_y: bool = false,
    floating_relative_to: bool = false,
};

fn patchConfigText(allocator: std.mem.Allocator, current: []const u8, config: config_types.Config) ![]u8 {
    var out = std.ArrayList(u8).empty;
    errdefer out.deinit(allocator);

    var section: ConfigSection = .root;
    var seen_sections = struct {
        capture: bool = false,
        capture_after: bool = false,
        capture_after_quick: bool = false,
        capture_after_area: bool = false,
        capture_after_fullscreen: bool = false,
        capture_after_all_screens: bool = false,
        notifications: bool = false,
        preview_window: bool = false,
        floating: bool = false,
    }{};
    var seen: SeenConfigFields = .{};

    var lines = std.mem.splitScalar(u8, current, '\n');
    while (lines.next()) |line| {
        const is_final_empty = line.len == 0 and lines.peek() == null and std.mem.endsWith(u8, current, "\n");
        if (is_final_empty) break;

        const trimmed_no_comment = std.mem.trim(u8, stripComment(line), " \t\r");
        if (trimmed_no_comment.len > 0 and trimmed_no_comment[0] == '[') {
            try appendMissingFields(allocator, &out, section, &seen, config);
            seen = .{};
            if (std.mem.eql(u8, trimmed_no_comment, "[capture]")) {
                section = .capture;
                seen_sections.capture = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[capture.after]")) {
                section = .capture_after;
                seen_sections.capture_after = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[capture.after.quick]")) {
                section = .capture_after_quick;
                seen_sections.capture_after_quick = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[capture.after.area]")) {
                section = .capture_after_area;
                seen_sections.capture_after_area = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[capture.after.fullscreen]")) {
                section = .capture_after_fullscreen;
                seen_sections.capture_after_fullscreen = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[capture.after.all_screens]")) {
                section = .capture_after_all_screens;
                seen_sections.capture_after_all_screens = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[notifications]")) {
                section = .notifications;
                seen_sections.notifications = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[preview.window]")) {
                section = .preview_window;
                seen_sections.preview_window = true;
            } else if (std.mem.eql(u8, trimmed_no_comment, "[preview.window.floating_position]")) {
                section = .preview_window_floating_position;
                seen_sections.floating = true;
            } else {
                section = .root;
            }
            try out.appendSlice(allocator, line);
            try out.append(allocator, '\n');
            continue;
        }

        if (try maybeAppendPatchedField(allocator, &out, section, &seen, line, config)) {
            continue;
        }
        try out.appendSlice(allocator, line);
        try out.append(allocator, '\n');
    }

    try appendMissingFields(allocator, &out, section, &seen, config);
    if (!seen_sections.capture) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture]\n");
        try appendMissingFields(allocator, &out, .capture, &seen, config);
    }
    if (!seen_sections.capture_after) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture.after]\n");
        try appendMissingFields(allocator, &out, .capture_after, &seen, config);
    }
    if (!seen_sections.capture_after_quick) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture.after.quick]\n");
        try appendMissingFields(allocator, &out, .capture_after_quick, &seen, config);
    }
    if (!seen_sections.capture_after_area) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture.after.area]\n");
        try appendMissingFields(allocator, &out, .capture_after_area, &seen, config);
    }
    if (!seen_sections.capture_after_fullscreen) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture.after.fullscreen]\n");
        try appendMissingFields(allocator, &out, .capture_after_fullscreen, &seen, config);
    }
    if (!seen_sections.capture_after_all_screens) {
        seen = .{};
        try out.appendSlice(allocator, "\n[capture.after.all_screens]\n");
        try appendMissingFields(allocator, &out, .capture_after_all_screens, &seen, config);
    }
    if (!seen_sections.notifications) {
        seen = .{};
        try out.appendSlice(allocator, "\n[notifications]\n");
        try appendMissingFields(allocator, &out, .notifications, &seen, config);
    }
    if (!seen_sections.preview_window) {
        seen = .{};
        try out.appendSlice(allocator, "\n[preview.window]\n");
        try appendMissingFields(allocator, &out, .preview_window, &seen, config);
    }
    if (!seen_sections.floating) {
        seen = .{};
        try out.appendSlice(allocator, "\n[preview.window.floating_position]\n");
        try appendMissingFields(allocator, &out, .preview_window_floating_position, &seen, config);
    }

    return out.toOwnedSlice(allocator);
}

fn appendMissingFields(allocator: std.mem.Allocator, out: *std.ArrayList(u8), section: ConfigSection, seen: *SeenConfigFields, config: config_types.Config) !void {
    const window = config.preview.window;
    switch (section) {
        .capture => {
            if (!seen.region_mode) {
                try out.print(allocator, "region_capture_mode = \"{s}\"\n", .{config.capture.region_capture_mode.asString()});
                seen.region_mode = true;
            }
        },
        .capture_after => {
            if (!seen.save_folder) {
                try out.print(allocator, "save_folder = \"{s}\"\n", .{config.capture.after.save_folder.value()});
                seen.save_folder = true;
            }
        },
        .capture_after_quick => try appendAfterModeMissing(allocator, out, seen, config.capture.after.quick),
        .capture_after_area => try appendAfterModeMissing(allocator, out, seen, config.capture.after.area),
        .capture_after_fullscreen => try appendAfterModeMissing(allocator, out, seen, config.capture.after.fullscreen),
        .capture_after_all_screens => try appendAfterModeMissing(allocator, out, seen, config.capture.after.all_screens),
        .notifications => {
            if (!seen.notifications_success) {
                try out.print(allocator, "success = {s}\n", .{boolText(config.notifications.success)});
                seen.notifications_success = true;
            }
            if (!seen.notifications_errors) {
                try out.print(allocator, "errors = {s}\n", .{boolText(config.notifications.errors)});
                seen.notifications_errors = true;
            }
            if (!seen.notifications_thumbnails) {
                try out.print(allocator, "thumbnails = {s}\n", .{boolText(config.notifications.thumbnails)});
                seen.notifications_thumbnails = true;
            }
        },
        .preview_window => {
            if (!seen.preview_mode) {
                try out.print(allocator, "mode = \"{s}\"\n", .{window.mode.asString()});
                seen.preview_mode = true;
            }
            if (!seen.focused) {
                try out.print(allocator, "focused = {s}\n", .{if (window.focused) "true" else "false"});
                seen.focused = true;
            }
            if (!seen.close_preview_on_save) {
                try out.print(allocator, "close_preview_on_save = {s}\n", .{if (window.close_preview_on_save) "true" else "false"});
                seen.close_preview_on_save = true;
            }
            if (!seen.width) {
                try out.print(allocator, "width = {d}\n", .{window.width orelse 1100});
                seen.width = true;
            }
            if (!seen.height) {
                try out.print(allocator, "height = {d}\n", .{window.height orelse 720});
                seen.height = true;
            }
            if (!seen.column_display) {
                try out.print(allocator, "default_column_display = \"{s}\"\n", .{window.default_column_display.asString()});
                seen.column_display = true;
            }
        },
        .preview_window_floating_position => {
            if (window.floating_position.x) |x| {
                if (!seen.floating_x) {
                    try out.print(allocator, "x = {d}\n", .{x});
                    seen.floating_x = true;
                }
            }
            if (window.floating_position.y) |y| {
                if (!seen.floating_y) {
                    try out.print(allocator, "y = {d}\n", .{y});
                    seen.floating_y = true;
                }
            }
            if (!seen.floating_relative_to) {
                try out.print(allocator, "relative_to = \"{s}\"\n", .{window.floating_position.relative_to.asString()});
                seen.floating_relative_to = true;
            }
        },
        .root => {},
    }
}

fn appendAfterModeMissing(allocator: std.mem.Allocator, out: *std.ArrayList(u8), seen: *SeenConfigFields, mode: config_types.CaptureAfterModeConfig) !void {
    if (!seen.after_skip_preview) {
        try out.print(allocator, "skip_preview = {s}\n", .{boolText(mode.skip_preview)});
        seen.after_skip_preview = true;
    }
    if (!seen.after_copy) {
        try out.print(allocator, "copy_to_clipboard = {s}\n", .{boolText(mode.copy_to_clipboard)});
        seen.after_copy = true;
    }
    if (!seen.after_save) {
        try out.print(allocator, "save_to_folder = {s}\n", .{boolText(mode.save_to_folder)});
        seen.after_save = true;
    }
}

fn maybeAppendPatchedField(
    allocator: std.mem.Allocator,
    out: *std.ArrayList(u8),
    section: ConfigSection,
    seen: *SeenConfigFields,
    line: []const u8,
    config: config_types.Config,
) !bool {
    const key = lineKey(line) orelse return false;
    const window = config.preview.window;
    switch (section) {
        .capture => {
            if (std.mem.eql(u8, key, "region_capture_mode")) {
                try out.print(allocator, "region_capture_mode = \"{s}\"\n", .{config.capture.region_capture_mode.asString()});
                seen.region_mode = true;
                return true;
            }
        },
        .capture_after => {
            if (std.mem.eql(u8, key, "save_folder")) {
                try out.print(allocator, "save_folder = \"{s}\"\n", .{config.capture.after.save_folder.value()});
                seen.save_folder = true;
                return true;
            }
        },
        .capture_after_quick => if (try maybeAppendAfterModePatched(allocator, out, seen, key, config.capture.after.quick)) return true,
        .capture_after_area => if (try maybeAppendAfterModePatched(allocator, out, seen, key, config.capture.after.area)) return true,
        .capture_after_fullscreen => if (try maybeAppendAfterModePatched(allocator, out, seen, key, config.capture.after.fullscreen)) return true,
        .capture_after_all_screens => if (try maybeAppendAfterModePatched(allocator, out, seen, key, config.capture.after.all_screens)) return true,
        .notifications => {
            if (std.mem.eql(u8, key, "success")) {
                try out.print(allocator, "success = {s}\n", .{boolText(config.notifications.success)});
                seen.notifications_success = true;
                return true;
            }
            if (std.mem.eql(u8, key, "errors")) {
                try out.print(allocator, "errors = {s}\n", .{boolText(config.notifications.errors)});
                seen.notifications_errors = true;
                return true;
            }
            if (std.mem.eql(u8, key, "thumbnails")) {
                try out.print(allocator, "thumbnails = {s}\n", .{boolText(config.notifications.thumbnails)});
                seen.notifications_thumbnails = true;
                return true;
            }
        },
        .preview_window => {
            if (std.mem.eql(u8, key, "mode")) {
                try out.print(allocator, "mode = \"{s}\"\n", .{window.mode.asString()});
                seen.preview_mode = true;
                return true;
            }
            if (std.mem.eql(u8, key, "focused")) {
                try out.print(allocator, "focused = {s}\n", .{if (window.focused) "true" else "false"});
                seen.focused = true;
                return true;
            }
            if (std.mem.eql(u8, key, "close_preview_on_save")) {
                try out.print(allocator, "close_preview_on_save = {s}\n", .{if (window.close_preview_on_save) "true" else "false"});
                seen.close_preview_on_save = true;
                return true;
            }
            if (std.mem.eql(u8, key, "width")) {
                try out.print(allocator, "width = {d}\n", .{window.width orelse 1100});
                seen.width = true;
                return true;
            }
            if (std.mem.eql(u8, key, "height")) {
                try out.print(allocator, "height = {d}\n", .{window.height orelse 720});
                seen.height = true;
                return true;
            }
            if (std.mem.eql(u8, key, "default_column_display")) {
                try out.print(allocator, "default_column_display = \"{s}\"\n", .{window.default_column_display.asString()});
                seen.column_display = true;
                return true;
            }
        },
        .preview_window_floating_position => {
            if (std.mem.eql(u8, key, "x")) {
                seen.floating_x = true;
                if (window.floating_position.x) |x| {
                    try out.print(allocator, "x = {d}\n", .{x});
                }
                return true;
            }
            if (std.mem.eql(u8, key, "y")) {
                seen.floating_y = true;
                if (window.floating_position.y) |y| {
                    try out.print(allocator, "y = {d}\n", .{y});
                }
                return true;
            }
            if (std.mem.eql(u8, key, "relative_to")) {
                try out.print(allocator, "relative_to = \"{s}\"\n", .{window.floating_position.relative_to.asString()});
                seen.floating_relative_to = true;
                return true;
            }
        },
        .root => {},
    }
    return false;
}

fn maybeAppendAfterModePatched(
    allocator: std.mem.Allocator,
    out: *std.ArrayList(u8),
    seen: *SeenConfigFields,
    key: []const u8,
    mode: config_types.CaptureAfterModeConfig,
) !bool {
    if (std.mem.eql(u8, key, "skip_preview")) {
        try out.print(allocator, "skip_preview = {s}\n", .{boolText(mode.skip_preview)});
        seen.after_skip_preview = true;
        return true;
    }
    if (std.mem.eql(u8, key, "copy_to_clipboard")) {
        try out.print(allocator, "copy_to_clipboard = {s}\n", .{boolText(mode.copy_to_clipboard)});
        seen.after_copy = true;
        return true;
    }
    if (std.mem.eql(u8, key, "save_to_folder")) {
        try out.print(allocator, "save_to_folder = {s}\n", .{boolText(mode.save_to_folder)});
        seen.after_save = true;
        return true;
    }
    return false;
}

fn boolText(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn lineKey(line: []const u8) ?[]const u8 {
    const no_comment = stripComment(line);
    const eq_index = std.mem.indexOfScalar(u8, no_comment, '=') orelse return null;
    const key = std.mem.trim(u8, no_comment[0..eq_index], " \t\r");
    if (key.len == 0) return null;
    return key;
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

pub fn defaultNiriConfigPath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("NIRI_CONFIG")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }
    if (environ.getPosix("XDG_CONFIG_HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return std.fmt.allocPrint(allocator, "{s}/niri/config.kdl", .{raw});
    }
    if (environ.getPosix("HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return std.fmt.allocPrint(allocator, "{s}/.config/niri/config.kdl", .{raw});
    }
    return error.ConfigUnreadable;
}

/// Install or replace Shaula's managed Niri window-rule block.
///
/// Contract constraints:
/// - only text between the Shaula markers is replaced.
/// - malformed Shaula markers fail with ConfigInvalid before backup/write.
/// - existing files get a timestamped backup before mutation.
/// - repeated installs with identical content are no-ops.
pub fn installNiriRule(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    kdl: []const u8,
    options: InstallOptions,
) !InstallResult {
    return installManagedBlock(allocator, io, environ, kdl, managed_block_begin, managed_block_end, options);
}

/// Install or replace a managed block with the given markers.
///
/// Contract constraints:
/// - only text between the given markers is replaced.
/// - malformed markers fail with ConfigInvalid before backup/write.
/// - existing files get a timestamped backup before mutation.
/// - repeated installs with identical content are no-ops.
pub fn installManagedBlock(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    kdl: []const u8,
    begin_marker: []const u8,
    end_marker: []const u8,
    options: InstallOptions,
) !InstallResult {
    const path = if (options.path_override) |override|
        try allocator.dupe(u8, override)
    else
        try defaultNiriConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const current = if (pathExists(io, path))
        try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024))
    else
        try allocator.dupe(u8, "");
    defer allocator.free(current);

    const block = try managedBlock(allocator, kdl, begin_marker, end_marker);
    defer allocator.free(block);

    const replacement = try replaceOrAppendManagedBlock(allocator, current, block, begin_marker, end_marker);
    defer allocator.free(replacement.content);

    if (std.mem.eql(u8, current, replacement.content)) {
        return .{
            .path = path,
            .backup_path = null,
            .installed = true,
            .replaced = replacement.replaced,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    var backup_path: ?[]u8 = null;
    if (!options.dry_run) {
        try ensureParentDir(io, path);
        if (current.len > 0) {
            backup_path = try backupExisting(allocator, io, path, current);
        }
        try writeFileAtomic(io, path, replacement.content);
    }

    return .{
        .path = path,
        .backup_path = backup_path,
        .installed = true,
        .replaced = replacement.replaced,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

/// Install or replace Shaula's managed Niri keybinds block.
///
/// Contract constraints:
/// - uses separate markers from the window-rule block.
/// - same idempotent, backup, and conflict-safe guarantees as `installManagedBlock`.
pub fn installNiriKeybinds(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    kdl: []const u8,
    options: InstallOptions,
) !InstallResult {
    return installManagedBlock(allocator, io, environ, kdl, niri_keybinds.managed_keybinds_begin, niri_keybinds.managed_keybinds_end, options);
}

pub const RemoveResult = struct {
    path: []u8,
    backup_path: ?[]u8,
    removed: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *RemoveResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
        if (self.backup_path) |path| allocator.free(path);
    }
};

/// Remove Shaula's managed Niri keybinds block.
///
/// Contract constraints:
/// - only the block between the keybind markers is removed.
/// - surrounding config is preserved unchanged.
/// - existing files get a timestamped backup before mutation.
/// - if no managed block exists, returns removed=false.
pub fn removeNiriKeybinds(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: InstallOptions,
) !RemoveResult {
    const path = if (options.path_override) |override|
        try allocator.dupe(u8, override)
    else
        try defaultNiriConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const current = if (pathExists(io, path))
        try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024))
    else
        try allocator.dupe(u8, "");
    defer allocator.free(current);

    const begin_marker = niri_keybinds.managed_keybinds_begin;
    const end_marker = niri_keybinds.managed_keybinds_end;
    const begin_count = countOccurrences(current, begin_marker);
    const end_count = countOccurrences(current, end_marker);

    if (begin_count == 0 and end_count == 0) {
        return .{
            .path = path,
            .backup_path = null,
            .removed = false,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    if (begin_count != 1 or end_count != 1) return error.ConfigInvalid;

    const begin = std.mem.indexOf(u8, current, begin_marker).?;
    const end = std.mem.indexOf(u8, current, end_marker).?;
    if (end < begin) return error.ConfigInvalid;

    var end_after = end + end_marker.len;
    if (end_after < current.len and current[end_after] == '\n') {
        end_after += 1;
    }
    const prefix = current[0..begin];
    const suffix = current[end_after..];
    const next = try std.fmt.allocPrint(allocator, "{s}{s}", .{ prefix, suffix });
    defer allocator.free(next);

    if (std.mem.eql(u8, current, next)) {
        return .{
            .path = path,
            .backup_path = null,
            .removed = true,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    var backup_path: ?[]u8 = null;
    if (!options.dry_run) {
        try ensureParentDir(io, path);
        if (current.len > 0) {
            backup_path = try backupExisting(allocator, io, path, current);
        }
        try writeFileAtomic(io, path, next);
    }

    return .{
        .path = path,
        .backup_path = backup_path,
        .removed = true,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

/// Scan the Niri config for CTRL+Shift+[1-4] conflicts outside the managed keybinds block.
///
/// Follows `include` directives to scan all referenced config files.
/// Returns owned Conflict slices; caller must free each Conflict and the slice.
pub fn detectKeybindConflicts(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    path_override: ?[]const u8,
) ![]const niri_keybinds.Conflict {
    const path = if (path_override) |override|
        try allocator.dupe(u8, override)
    else
        try defaultNiriConfigPath(allocator, environ);
    defer allocator.free(path);

    var all_content = std.ArrayList(u8).empty;
    defer all_content.deinit(allocator);

    try collectConfigWithIncludes(allocator, io, path, &all_content, 0);

    return niri_keybinds.detectConflicts(allocator, all_content.items);
}

const max_include_depth = 8;

/// Recursively read a Niri config file and its includes into a single buffer.
fn collectConfigWithIncludes(
    allocator: std.mem.Allocator,
    io: std.Io,
    path: []const u8,
    out: *std.ArrayList(u8),
    depth: u32,
) !void {
    if (depth >= max_include_depth) return;

    const content = if (pathExists(io, path))
        try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024))
    else
        return;
    defer allocator.free(content);

    try out.appendSlice(allocator, content);
    if (content.len > 0 and content[content.len - 1] != '\n') {
        try out.append(allocator, '\n');
    }

    const dir = std.fs.path.dirname(path) orelse ".";
    var lines = std.mem.splitScalar(u8, content, '\n');
    while (lines.next()) |line| {
        const trimmed = std.mem.trim(u8, line, " \t\r");
        if (!std.mem.startsWith(u8, trimmed, "include ")) continue;

        const after_include = std.mem.trim(u8, trimmed["include ".len..], " \t");
        if (after_include.len < 2) continue;
        if (after_include[0] != '"') continue;

        const end_quote = std.mem.indexOfScalar(u8, after_include[1..], '"') orelse continue;
        const raw_path = after_include[1 .. 1 + end_quote];

        const resolved = if (std.fs.path.isAbsolute(raw_path))
            try allocator.dupe(u8, raw_path)
        else
            try std.fmt.allocPrint(allocator, "{s}/{s}", .{ dir, raw_path });
        defer allocator.free(resolved);

        try collectConfigWithIncludes(allocator, io, resolved, out, depth + 1);
    }
}

fn managedBlock(allocator: std.mem.Allocator, kdl: []const u8, begin_marker: []const u8, end_marker: []const u8) ![]u8 {
    return std.fmt.allocPrint(allocator, "{s}\n{s}{s}{s}\n", .{
        begin_marker,
        kdl,
        if (std.mem.endsWith(u8, kdl, "\n")) "" else "\n",
        end_marker,
    });
}

const ManagedReplaceResult = struct {
    content: []u8,
    replaced: bool,
};

fn replaceOrAppendManagedBlock(allocator: std.mem.Allocator, current: []const u8, block: []const u8, begin_marker: []const u8, end_marker: []const u8) !ManagedReplaceResult {
    const begin_count = countOccurrences(current, begin_marker);
    const end_count = countOccurrences(current, end_marker);

    if (begin_count == 0 and end_count == 0) {
        const separator = if (current.len == 0 or std.mem.endsWith(u8, current, "\n")) "" else "\n";
        return .{
            .content = try std.fmt.allocPrint(allocator, "{s}{s}\n{s}", .{ current, separator, block }),
            .replaced = false,
        };
    }

    if (begin_count != 1 or end_count != 1) return error.ConfigInvalid;

    const begin = std.mem.indexOf(u8, current, begin_marker).?;
    const end = std.mem.indexOf(u8, current, end_marker).?;
    if (end < begin) return error.ConfigInvalid;

    var end_after = end + end_marker.len;
    if (end_after < current.len and current[end_after] == '\n') {
        end_after += 1;
    }
    const prefix = current[0..begin];
    const suffix = current[end_after..];
    return .{
        .content = try std.fmt.allocPrint(allocator, "{s}{s}{s}", .{ prefix, block, suffix }),
        .replaced = true,
    };
}

fn countOccurrences(haystack: []const u8, needle: []const u8) usize {
    var count: usize = 0;
    var cursor: usize = 0;
    while (std.mem.indexOfPos(u8, haystack, cursor, needle)) |index| {
        count += 1;
        cursor = index + needle.len;
    }
    return count;
}

fn backupExisting(allocator: std.mem.Allocator, io: std.Io, path: []const u8, current: []const u8) ![]u8 {
    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    return backupExistingWithBase(allocator, io, path, current, millis);
}

fn backupExistingWithBase(allocator: std.mem.Allocator, io: std.Io, path: []const u8, current: []const u8, base: i64) ![]u8 {
    var attempt: usize = 0;
    while (attempt < max_backup_attempts) : (attempt += 1) {
        const backup_path = if (attempt == 0)
            try std.fmt.allocPrint(allocator, "{s}.shaula-backup-{d}", .{ path, base })
        else
            try std.fmt.allocPrint(allocator, "{s}.shaula-backup-{d}-{d}", .{ path, base, attempt });
        errdefer allocator.free(backup_path);

        writeFileExclusive(io, backup_path, current) catch |err| switch (err) {
            error.PathAlreadyExists => {
                allocator.free(backup_path);
                continue;
            },
            else => return err,
        };
        return backup_path;
    }
    return error.ConfigUnreadable;
}

fn writeFileExclusive(io: std.Io, path: []const u8, contents: []const u8) !void {
    var file = if (std.fs.path.isAbsolute(path))
        try std.Io.Dir.createFileAbsolute(io, path, .{ .exclusive = true })
    else
        try std.Io.Dir.cwd().createFile(io, path, .{ .exclusive = true });
    var file_open = true;
    defer if (file_open) file.close(io);

    var buffer: [4096]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll(contents);
    try writer.interface.flush();
    file.close(io);
    file_open = false;
}

pub fn pathExists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
}

fn ensureParentDir(io: std.Io, path: []const u8) !void {
    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }
}

fn writeFileAtomic(io: std.Io, path: []const u8, contents: []const u8) !void {
    const tmp_path = try std.fmt.allocPrint(std.heap.page_allocator, "{s}.tmp", .{path});
    defer std.heap.page_allocator.free(tmp_path);

    var file = if (std.fs.path.isAbsolute(tmp_path))
        try std.Io.Dir.createFileAbsolute(io, tmp_path, .{ .truncate = true })
    else
        try std.Io.Dir.cwd().createFile(io, tmp_path, .{ .truncate = true });
    var file_open = true;
    defer if (file_open) file.close(io);

    var buffer: [4096]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll(contents);
    try writer.interface.flush();
    file.close(io);
    file_open = false;

    if (std.fs.path.isAbsolute(tmp_path) and std.fs.path.isAbsolute(path)) {
        try std.Io.Dir.renameAbsolute(tmp_path, path, io);
    } else {
        try std.Io.Dir.cwd().rename(tmp_path, std.Io.Dir.cwd(), path, io);
    }
}

test "managed block appends when missing" {
    const block = try managedBlock(std.testing.allocator, "window-rule {}\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    const result = try replaceOrAppendManagedBlock(std.testing.allocator, "input\n", block, managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(result.content);
    try std.testing.expect(!result.replaced);
    try std.testing.expect(std.mem.indexOf(u8, result.content, managed_block_begin) != null);
}

test "managed block replaces existing block only" {
    const old =
        \\before
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\old
        \\// END SHAULA PREVIEW WINDOW RULE
        \\after
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    const result = try replaceOrAppendManagedBlock(std.testing.allocator, old, block, managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(result.content);
    try std.testing.expect(result.replaced);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "before") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "after") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "old") == null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "new") != null);
}

test "managed block replacement is idempotent" {
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    const first = try replaceOrAppendManagedBlock(std.testing.allocator, "input\n", block, managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(first.content);
    const second = try replaceOrAppendManagedBlock(std.testing.allocator, first.content, block, managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(second.content);
    try std.testing.expectEqualStrings(first.content, second.content);
}

test "managed block rejects begin without end" {
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, managed_block_begin, block, managed_block_begin, managed_block_end));
}

test "managed block rejects end without begin" {
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, managed_block_end, block, managed_block_begin, managed_block_end));
}

test "managed block rejects end before begin" {
    const current =
        \\// END SHAULA PREVIEW WINDOW RULE
        \\body
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block, managed_block_begin, managed_block_end));
}

test "managed block rejects duplicate begin markers" {
    const current =
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\first
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\// END SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block, managed_block_begin, managed_block_end));
}

test "managed block rejects duplicate end markers" {
    const current =
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\first
        \\// END SHAULA PREVIEW WINDOW RULE
        \\// END SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n", managed_block_begin, managed_block_end);
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block, managed_block_begin, managed_block_end));
}

test "backup creation never overwrites existing backup path" {
    const io = std.testing.io;
    const source_path = "/tmp/shaula-manager-backup-test.kdl";
    const existing_backup = "/tmp/shaula-manager-backup-test.kdl.shaula-backup-42";
    const expected_backup = "/tmp/shaula-manager-backup-test.kdl.shaula-backup-42-1";

    std.Io.Dir.deleteFileAbsolute(io, source_path) catch {};
    std.Io.Dir.deleteFileAbsolute(io, existing_backup) catch {};
    std.Io.Dir.deleteFileAbsolute(io, expected_backup) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, source_path) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, existing_backup) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, expected_backup) catch {};

    try writeFileAtomic(io, existing_backup, "existing");
    const backup_path = try backupExistingWithBase(std.testing.allocator, io, source_path, "new", 42);
    defer std.testing.allocator.free(backup_path);

    try std.testing.expectEqualStrings(expected_backup, backup_path);

    const existing_contents = try std.Io.Dir.cwd().readFileAlloc(io, existing_backup, std.testing.allocator, .limited(1024));
    defer std.testing.allocator.free(existing_contents);
    try std.testing.expectEqualStrings("existing", existing_contents);

    const backup_contents = try std.Io.Dir.cwd().readFileAlloc(io, expected_backup, std.testing.allocator, .limited(1024));
    defer std.testing.allocator.free(backup_contents);
    try std.testing.expectEqualStrings("new", backup_contents);
}
