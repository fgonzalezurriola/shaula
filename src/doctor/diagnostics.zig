const std = @import("std");

const loader = @import("../config/loader.zig");
const env = @import("../runtime/env.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const tool_lookup = @import("../runtime/tool_lookup.zig");

pub const ToolStatus = struct {
    name: []const u8,
    path: ?[]u8 = null,

    pub fn found(self: ToolStatus) bool {
        return self.path != null;
    }

    fn deinit(self: *ToolStatus, allocator: std.mem.Allocator) void {
        if (self.path) |path| allocator.free(path);
        self.path = null;
    }
};

pub const Report = struct {
    binary_path: ?[:0]u8,
    xdg_config_dir: ?[]u8,
    config_file_path: ?[]u8,
    config_exists: bool,
    generated_dir_path: ?[]u8,
    niri_snippet_path: ?[]u8,
    niri_snippet_exists: bool,
    xdg_session_type: ?[]const u8,
    wayland_display: ?[]const u8,
    detected_compositor: []const u8,
    backend_active: []const u8,
    portal_available: bool,
    portal_window_capable: bool,
    overlay_supported: bool,
    niri_config_env: ?[]const u8,
    niri_config_env_exists: bool,
    niri_user_config_exists: bool,
    niri_cfg_dir_exists: bool,
    niri_etc_config_exists: bool,
    noctalia_dir_exists: bool,
    noctalia_plugins_dir_exists: bool,
    noctalia_plugins_json_exists: bool,
    noctalia_settings_json_exists: bool,
    noctalia_shaula_plugin_dir_exists: bool,
    noctalia_shaula_plugin_enabled: ?bool,
    tools: []ToolStatus,
    warnings: []const []const u8,

    pub fn deinit(self: *Report, allocator: std.mem.Allocator) void {
        if (self.binary_path) |path| allocator.free(path);
        if (self.xdg_config_dir) |path| allocator.free(path);
        if (self.config_file_path) |path| allocator.free(path);
        if (self.generated_dir_path) |path| allocator.free(path);
        if (self.niri_snippet_path) |path| allocator.free(path);
        for (self.tools) |*tool| tool.deinit(allocator);
        allocator.free(self.tools);
        allocator.free(self.warnings);
    }
};

const tool_names = [_][]const u8{ "grim", "wl-copy", "wl-paste", "gdbus", "niri", "quickshell" };

/// Collect install and runtime diagnostics without mutating user state.
///
/// Contract constraint: this Module mirrors installer detection rules for Niri
/// and Noctalia readiness; renderer modules must not rediscover these paths.
pub fn collect(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !Report {
    const binary_path = std.process.executablePathAlloc(io, allocator) catch null;
    const xdg_config_dir = try resolveXdgConfigDir(allocator, environ);
    const config_file_path = try loader.resolveConfigPath(allocator, environ);
    errdefer if (config_file_path) |path| allocator.free(path);
    const generated_dir_path = if (xdg_config_dir) |dir| try std.fmt.allocPrint(allocator, "{s}/shaula/generated", .{dir}) else null;
    const niri_snippet_path = if (generated_dir_path) |dir| try std.fmt.allocPrint(allocator, "{s}/niri-shaula.kdl", .{dir}) else null;

    var tools = try allocator.alloc(ToolStatus, tool_names.len);
    errdefer allocator.free(tools);
    for (tool_names, 0..) |name, index| {
        tools[index] = .{ .name = name, .path = try tool_lookup.findInPathAlloc(allocator, io, environ, name) };
    }

    const niri_config_env = env.slice(environ, "NIRI_CONFIG");
    const wayland_display = env.slice(environ, "WAYLAND_DISPLAY");
    const xdg_session_type = env.slice(environ, "XDG_SESSION_TYPE");
    const runtime = runtime_capabilities.resolve(allocator, io, environ);
    const niri_user_config_path = try xdgConfigPath(allocator, xdg_config_dir, "niri/config.kdl");
    defer if (niri_user_config_path) |path| allocator.free(path);
    const niri_cfg_dir_path = try xdgConfigPath(allocator, xdg_config_dir, "niri/cfg");
    defer if (niri_cfg_dir_path) |path| allocator.free(path);
    const noctalia_dir_path = try xdgConfigPath(allocator, xdg_config_dir, "noctalia");
    defer if (noctalia_dir_path) |path| allocator.free(path);
    const noctalia_plugins_dir_path = try xdgConfigPath(allocator, xdg_config_dir, "noctalia/plugins");
    defer if (noctalia_plugins_dir_path) |path| allocator.free(path);
    const noctalia_plugins_json_path = try xdgConfigPath(allocator, xdg_config_dir, "noctalia/plugins.json");
    defer if (noctalia_plugins_json_path) |path| allocator.free(path);
    const noctalia_settings_json_path = try xdgConfigPath(allocator, xdg_config_dir, "noctalia/settings.json");
    defer if (noctalia_settings_json_path) |path| allocator.free(path);
    const noctalia_shaula_plugin_dir_path = try xdgConfigPath(allocator, xdg_config_dir, "noctalia/plugins/shaula");
    defer if (noctalia_shaula_plugin_dir_path) |path| allocator.free(path);

    var warnings = std.ArrayList([]const u8).empty;
    errdefer warnings.deinit(allocator);
    if (!toolFound(tools, "grim")) try warnings.append(allocator, "missing grim");
    if (!toolFound(tools, "wl-copy")) try warnings.append(allocator, "missing wl-copy");
    if (wayland_display == null or wayland_display.?.len == 0) try warnings.append(allocator, "missing WAYLAND_DISPLAY");
    if (xdg_session_type == null or !std.mem.eql(u8, xdg_session_type.?, "wayland")) try warnings.append(allocator, "XDG_SESSION_TYPE is not wayland");
    if (!runtime.portal_available and runtime.compositor.kind == .wayland and !runtime.overlay_supported) try warnings.append(allocator, "portal unavailable for generic Wayland compositor");

    const niri_detected =
        toolFound(tools, "niri") or
        (niri_config_env != null and niri_config_env.?.len > 0) or
        (niri_user_config_path != null and tool_lookup.fileExists(io, niri_user_config_path.?)) or
        (niri_cfg_dir_path != null and tool_lookup.fileExists(io, niri_cfg_dir_path.?)) or
        tool_lookup.fileExists(io, "/etc/niri/config.kdl");
    const niri_snippet_exists = niri_snippet_path != null and tool_lookup.fileExists(io, niri_snippet_path.?);
    if (niri_detected and !niri_snippet_exists) try warnings.append(allocator, "Niri detected but generated snippet is missing");

    const noctalia_detected =
        (noctalia_dir_path != null and tool_lookup.fileExists(io, noctalia_dir_path.?)) or
        (noctalia_plugins_dir_path != null and tool_lookup.fileExists(io, noctalia_plugins_dir_path.?)) or
        (noctalia_plugins_json_path != null and tool_lookup.fileExists(io, noctalia_plugins_json_path.?)) or
        (noctalia_settings_json_path != null and tool_lookup.fileExists(io, noctalia_settings_json_path.?));
    const noctalia_shaula_plugin_dir_exists = noctalia_shaula_plugin_dir_path != null and tool_lookup.fileExists(io, noctalia_shaula_plugin_dir_path.?);
    const noctalia_shaula_plugin_enabled = if (noctalia_plugins_json_path) |path| try detectNoctaliaPluginEnabled(allocator, io, path) else null;
    if (noctalia_detected and !noctalia_shaula_plugin_dir_exists) try warnings.append(allocator, "Noctalia detected but Shaula plugin is not installed");

    return .{
        .binary_path = binary_path,
        .xdg_config_dir = xdg_config_dir,
        .config_file_path = config_file_path,
        .config_exists = config_file_path != null and tool_lookup.fileExists(io, config_file_path.?),
        .generated_dir_path = generated_dir_path,
        .niri_snippet_path = niri_snippet_path,
        .niri_snippet_exists = niri_snippet_exists,
        .xdg_session_type = xdg_session_type,
        .wayland_display = wayland_display,
        .detected_compositor = runtime.compositor.label,
        .backend_active = runtime_capabilities.backendLabel(runtime.backend),
        .portal_available = runtime.portal_available,
        .portal_window_capable = runtime.portal_window_capable,
        .overlay_supported = runtime.overlay_supported,
        .niri_config_env = niri_config_env,
        .niri_config_env_exists = niri_config_env != null and niri_config_env.?.len > 0 and tool_lookup.fileExists(io, niri_config_env.?),
        .niri_user_config_exists = niri_user_config_path != null and tool_lookup.fileExists(io, niri_user_config_path.?),
        .niri_cfg_dir_exists = niri_cfg_dir_path != null and tool_lookup.fileExists(io, niri_cfg_dir_path.?),
        .niri_etc_config_exists = tool_lookup.fileExists(io, "/etc/niri/config.kdl"),
        .noctalia_dir_exists = noctalia_dir_path != null and tool_lookup.fileExists(io, noctalia_dir_path.?),
        .noctalia_plugins_dir_exists = noctalia_plugins_dir_path != null and tool_lookup.fileExists(io, noctalia_plugins_dir_path.?),
        .noctalia_plugins_json_exists = noctalia_plugins_json_path != null and tool_lookup.fileExists(io, noctalia_plugins_json_path.?),
        .noctalia_settings_json_exists = noctalia_settings_json_path != null and tool_lookup.fileExists(io, noctalia_settings_json_path.?),
        .noctalia_shaula_plugin_dir_exists = noctalia_shaula_plugin_dir_exists,
        .noctalia_shaula_plugin_enabled = noctalia_shaula_plugin_enabled,
        .tools = tools,
        .warnings = try warnings.toOwnedSlice(allocator),
    };
}

pub fn toolFound(tools: []const ToolStatus, name: []const u8) bool {
    for (tools) |tool| {
        if (std.mem.eql(u8, tool.name, name)) return tool.found();
    }
    return false;
}

fn resolveXdgConfigDir(allocator: std.mem.Allocator, environ: std.process.Environ) !?[]u8 {
    if (env.slice(environ, "XDG_CONFIG_HOME")) |value| {
        if (value.len > 0) return try allocator.dupe(u8, value);
    }
    if (env.slice(environ, "HOME")) |home| {
        if (home.len > 0) return try std.fmt.allocPrint(allocator, "{s}/.config", .{home});
    }
    return null;
}

fn xdgConfigPath(allocator: std.mem.Allocator, xdg_config_dir: ?[]const u8, suffix: []const u8) !?[]u8 {
    const dir = xdg_config_dir orelse return null;
    return try std.fmt.allocPrint(allocator, "{s}/{s}", .{ dir, suffix });
}

/// Reads Noctalia's plugin registry without mutating it.
///
/// The installer-owned contract is `states.shaula.enabled`; malformed or
/// unfamiliar JSON returns null so doctor does not over-claim plugin state.
fn detectNoctaliaPluginEnabled(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !?bool {
    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024)) catch return null;
    defer allocator.free(raw);
    var parsed = std.json.parseFromSlice(std.json.Value, allocator, raw, .{}) catch return null;
    defer parsed.deinit();

    const root = switch (parsed.value) {
        .object => |object| object,
        else => return null,
    };
    const states_value = root.get("states") orelse return null;
    const states = switch (states_value) {
        .object => |object| object,
        else => return null,
    };
    const shaula_value = states.get("shaula") orelse return false;
    const shaula = switch (shaula_value) {
        .object => |object| object,
        else => return null,
    };
    const enabled_value = shaula.get("enabled") orelse return false;
    return switch (enabled_value) {
        .bool => |value| value,
        else => null,
    };
}
