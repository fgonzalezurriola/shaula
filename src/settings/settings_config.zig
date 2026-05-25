const std = @import("std");
const c_compat = @import("c_compat");

const CInt = c_int;

const TRUE: CInt = 1;
const FALSE: CInt = 0;

extern fn g_free(mem: ?*anyopaque) void;
extern fn g_getenv(variable: [*:0]const u8) ?[*:0]const u8;
extern fn g_get_home_dir() ?[*:0]const u8;

const RegionMode = enum(CInt) {
    live = 0,
    frozen = 1,
};

const WindowMode = enum(CInt) {
    auto = 0,
    tiling = 1,
    floating = 2,
    maximized = 3,
    maximized_to_edges = 4,
    fullscreen = 5,
};

const SizePreset = enum(CInt) {
    small = 0,
    medium = 1,
    large = 2,
};

const PositionPreset = enum(CInt) {
    centered = 0,
    top_left = 1,
    top_right = 2,
};

const ShaulaSettingsConfig = extern struct {
    region_mode: RegionMode,
    window_mode: WindowMode,
    focused: CInt,
    close_preview_on_save: CInt,
    width: CInt,
    height: CInt,
    column_display: ?[*:0]u8,
    floating_x_set: CInt,
    floating_y_set: CInt,
    floating_x: CInt,
    floating_y: CInt,
    floating_relative_to: ?[*:0]u8,
    position_preset: PositionPreset,
    quick_skip_preview: CInt,
    quick_copy: CInt,
    quick_save: CInt,
    area_skip_preview: CInt,
    area_copy: CInt,
    area_save: CInt,
    fullscreen_skip_preview: CInt,
    fullscreen_copy: CInt,
    fullscreen_save: CInt,
    all_screens_skip_preview: CInt,
    all_screens_copy: CInt,
    all_screens_save: CInt,
    save_folder: ?[*:0]u8,
    notifications_success: CInt,
    notifications_errors: CInt,
    notifications_thumbnails: CInt,
};

export fn shaula_settings_config_init_defaults(config: *ShaulaSettingsConfig) void {
    config.* = .{
        .region_mode = .frozen,
        .window_mode = .floating,
        .focused = TRUE,
        .close_preview_on_save = TRUE,
        .width = 1100,
        .height = 720,
        .column_display = c_compat.dupZ("normal"),
        .floating_x_set = FALSE,
        .floating_y_set = FALSE,
        .floating_x = 0,
        .floating_y = 0,
        .floating_relative_to = c_compat.dupZ("top-left"),
        .position_preset = .centered,
        .quick_skip_preview = FALSE,
        .quick_copy = TRUE,
        .quick_save = FALSE,
        .area_skip_preview = FALSE,
        .area_copy = TRUE,
        .area_save = FALSE,
        .fullscreen_skip_preview = TRUE,
        .fullscreen_copy = TRUE,
        .fullscreen_save = TRUE,
        .all_screens_skip_preview = TRUE,
        .all_screens_copy = TRUE,
        .all_screens_save = TRUE,
        .save_folder = c_compat.dupZ("~/Pictures/shaula"),
        .notifications_success = TRUE,
        .notifications_errors = TRUE,
        .notifications_thumbnails = TRUE,
    };
}

export fn shaula_settings_config_clear(config: *ShaulaSettingsConfig) void {
    g_free(config.column_display);
    g_free(config.floating_relative_to);
    g_free(config.save_folder);
    config.column_display = null;
    config.floating_relative_to = null;
    config.save_folder = null;
}

export fn shaula_settings_region_mode_text(mode: RegionMode) [*:0]const u8 {
    return switch (mode) {
        .frozen => "frozen",
        .live => "live",
    };
}

export fn shaula_settings_window_mode_text(mode: WindowMode) [*:0]const u8 {
    return switch (mode) {
        .auto => "auto",
        .tiling => "tiling",
        .maximized => "maximized",
        .maximized_to_edges => "maximized-to-edges",
        .fullscreen => "fullscreen",
        .floating => "floating",
    };
}

export fn shaula_settings_size_preset_for_config(config: *const ShaulaSettingsConfig) SizePreset {
    if (config.width == 900 and config.height == 600) return .small;
    if (config.width == 1100 and config.height == 720) return .medium;
    if (config.width == 1400 and config.height == 900) return .large;
    return .small;
}

export fn shaula_settings_apply_size_preset(config: *ShaulaSettingsConfig, preset: SizePreset) void {
    switch (preset) {
        .small => {
            config.width = 900;
            config.height = 600;
        },
        .medium => {
            config.width = 1100;
            config.height = 720;
        },
        .large => {
            config.width = 1400;
            config.height = 900;
        },
    }
}

export fn shaula_settings_apply_position_preset(config: *ShaulaSettingsConfig, preset: PositionPreset) void {
    config.position_preset = preset;
    switch (preset) {
        .centered => {
            config.floating_x_set = FALSE;
            config.floating_y_set = FALSE;
            replaceString(&config.floating_relative_to, "top-left");
        },
        .top_left, .top_right => {
            config.floating_x_set = TRUE;
            config.floating_y_set = TRUE;
            config.floating_x = 80;
            config.floating_y = 80;
            replaceString(&config.floating_relative_to, if (preset == .top_left) "top-left" else "top-right");
        },
    }
}

export fn shaula_settings_position_arg(config: *const ShaulaSettingsConfig) [*:0]const u8 {
    return switch (config.position_preset) {
        .top_left => "top-left",
        .top_right => "top-right",
        .centered => "centered",
    };
}

export fn shaula_settings_resolve_config_path() ?[*:0]u8 {
    if (g_getenv("SHAULA_CONFIG_FILE")) |raw| {
        const trimmed = std.mem.trim(u8, std.mem.span(raw), " \t\r\n");
        if (trimmed.len > 0) return c_compat.dupZ(trimmed);
    }
    if (g_getenv("XDG_CONFIG_HOME")) |raw| {
        const xdg = std.mem.span(raw);
        if (xdg.len > 0) return c_compat.joinPathZ(&.{ xdg, "shaula", "config.toml" });
    }
    if (g_get_home_dir()) |raw| {
        const home = std.mem.span(raw);
        if (home.len > 0) return c_compat.joinPathZ(&.{ home, ".config", "shaula", "config.toml" });
    }
    return null;
}

export fn shaula_settings_config_path_from_show_json(json_z: ?[*:0]const u8) ?[*:0]u8 {
    const json = spanOrEmpty(json_z);
    return jsonStringAfter(json, "\"path\":\"");
}

export fn shaula_settings_config_from_show_json(json_z: ?[*:0]const u8, config: *ShaulaSettingsConfig) CInt {
    const json = if (json_z) |value| std.mem.span(value) else return FALSE;

    if (jsonStringAfter(json, "\"region_capture_mode\":\"")) |region| {
        defer g_free(region);
        config.region_mode = if (std.mem.eql(u8, std.mem.span(region), "frozen")) .frozen else .live;
    }
    if (jsonStringAfter(json, "\"mode\":\"")) |mode| {
        defer g_free(mode);
        config.window_mode = parseWindowMode(std.mem.span(mode));
    }

    config.focused = jsonBoolAfter(json, "\"focused\":", config.focused);
    config.close_preview_on_save = jsonBoolAfter(json, "\"close_preview_on_save\":", config.close_preview_on_save);
    config.width = jsonIntAfter(json, "\"width\":", config.width);
    config.height = jsonIntAfter(json, "\"height\":", config.height);

    if (jsonStringAfter(json, "\"default_column_display\":\"")) |display| {
        g_free(config.column_display);
        config.column_display = display;
    }

    var parsed_x: CInt = 0;
    var parsed_y: CInt = 0;
    config.floating_x_set = jsonNullableIntAfter(json, "\"x\":", &parsed_x);
    config.floating_y_set = jsonNullableIntAfter(json, "\"y\":", &parsed_y);
    if (config.floating_x_set == TRUE) config.floating_x = parsed_x;
    if (config.floating_y_set == TRUE) config.floating_y = parsed_y;

    if (jsonStringAfter(json, "\"relative_to\":\"")) |relative| {
        g_free(config.floating_relative_to);
        config.floating_relative_to = relative;
    }

    config.position_preset = classifyPosition(config);

    parseAfterMode(json, "\"quick\":{", &config.quick_skip_preview, &config.quick_copy, &config.quick_save);
    parseAfterMode(json, "\"area\":{", &config.area_skip_preview, &config.area_copy, &config.area_save);
    parseAfterMode(json, "\"fullscreen\":{", &config.fullscreen_skip_preview, &config.fullscreen_copy, &config.fullscreen_save);
    parseAfterMode(json, "\"all_screens\":{", &config.all_screens_skip_preview, &config.all_screens_copy, &config.all_screens_save);
    if (jsonStringAfter(json, "\"save_folder\":\"")) |folder| {
        g_free(config.save_folder);
        config.save_folder = folder;
    }
    config.notifications_success = jsonBoolAfter(json, "\"success\":", config.notifications_success);
    config.notifications_errors = jsonBoolAfter(json, "\"errors\":", config.notifications_errors);
    config.notifications_thumbnails = jsonBoolAfter(json, "\"thumbnails\":", config.notifications_thumbnails);
    return TRUE;
}

fn parseAfterMode(json: []const u8, object_needle: []const u8, skip: *CInt, copy: *CInt, save: *CInt) void {
    const start = std.mem.indexOf(u8, json, object_needle) orelse return;
    const body_start = start + object_needle.len;
    const rel_end = std.mem.indexOfScalar(u8, json[body_start..], '}') orelse return;
    const body = json[body_start .. body_start + rel_end];
    skip.* = jsonBoolAfter(body, "\"skip_preview\":", skip.*);
    copy.* = jsonBoolAfter(body, "\"copy_to_clipboard\":", copy.*);
    save.* = jsonBoolAfter(body, "\"save_to_folder\":", save.*);
}

fn parseWindowMode(value: []const u8) WindowMode {
    if (std.mem.eql(u8, value, "auto")) return .auto;
    if (std.mem.eql(u8, value, "tiling")) return .tiling;
    if (std.mem.eql(u8, value, "maximized")) return .maximized;
    if (std.mem.eql(u8, value, "maximized-to-edges")) return .maximized_to_edges;
    if (std.mem.eql(u8, value, "fullscreen")) return .fullscreen;
    return .floating;
}

fn classifyPosition(config: *const ShaulaSettingsConfig) PositionPreset {
    if (config.floating_x_set != TRUE or config.floating_y_set != TRUE) return .centered;
    const relative = if (config.floating_relative_to) |value| std.mem.span(value) else "";
    if (config.floating_x == 80 and config.floating_y == 80 and std.mem.eql(u8, relative, "top-left")) return .top_left;
    if (config.floating_x == 80 and config.floating_y == 80 and std.mem.eql(u8, relative, "top-right")) return .top_right;
    return .centered;
}

fn jsonStringAfter(json: []const u8, needle: []const u8) ?[*:0]u8 {
    const start = std.mem.indexOf(u8, json, needle) orelse return null;
    const value_start = start + needle.len;
    const rel_end = std.mem.indexOfScalar(u8, json[value_start..], '"') orelse return null;
    return c_compat.dupZ(json[value_start .. value_start + rel_end]);
}

fn jsonBoolAfter(json: []const u8, needle: []const u8, fallback: CInt) CInt {
    const start = std.mem.indexOf(u8, json, needle) orelse return fallback;
    const tail = json[start + needle.len ..];
    if (std.mem.startsWith(u8, tail, "true")) return TRUE;
    if (std.mem.startsWith(u8, tail, "false")) return FALSE;
    return fallback;
}

fn jsonIntAfter(json: []const u8, needle: []const u8, fallback: CInt) CInt {
    const start = std.mem.indexOf(u8, json, needle) orelse return fallback;
    const tail = json[start + needle.len ..];
    if (std.mem.startsWith(u8, tail, "null")) return fallback;
    return parseLeadingInt(tail) orelse fallback;
}

fn jsonNullableIntAfter(json: []const u8, needle: []const u8, out: *c_int) CInt {
    const start = std.mem.indexOf(u8, json, needle) orelse return FALSE;
    const tail = json[start + needle.len ..];
    if (std.mem.startsWith(u8, tail, "null")) return FALSE;
    out.* = parseLeadingInt(tail) orelse return FALSE;
    return TRUE;
}

fn parseLeadingInt(value: []const u8) ?c_int {
    var end: usize = 0;
    if (end < value.len and (value[end] == '-' or value[end] == '+')) end += 1;
    const digits_start = end;
    while (end < value.len and std.ascii.isDigit(value[end])) end += 1;
    if (end == digits_start) return null;
    const parsed = std.fmt.parseInt(i64, value[0..end], 10) catch return null;
    if (parsed < std.math.minInt(CInt) or parsed > std.math.maxInt(CInt)) return null;
    return @intCast(parsed);
}

fn replaceString(slot: *?[*:0]u8, value: []const u8) void {
    g_free(slot.*);
    slot.* = c_compat.dupZ(value);
}

fn spanOrEmpty(value: ?[*:0]const u8) []const u8 {
    return if (value) |v| std.mem.span(v) else "";
}
