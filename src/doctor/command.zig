const std = @import("std");

const command_json = @import("../capture/command_json.zig");
const loader = @import("../config/loader.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

const ToolStatus = struct {
    name: []const u8,
    path: ?[]u8 = null,

    fn found(self: ToolStatus) bool {
        return self.path != null;
    }

    fn deinit(self: *ToolStatus, allocator: std.mem.Allocator) void {
        if (self.path) |path| allocator.free(path);
        self.path = null;
    }
};

const DoctorReport = struct {
    binary_path: ?[:0]u8,
    xdg_config_dir: ?[]u8,
    config_file_path: ?[]u8,
    config_exists: bool,
    generated_dir_path: ?[]u8,
    niri_snippet_path: ?[]u8,
    niri_snippet_exists: bool,
    xdg_session_type: ?[]const u8,
    wayland_display: ?[]const u8,
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

    fn deinit(self: *DoctorReport, allocator: std.mem.Allocator) void {
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

/// Report install and runtime diagnostics without modifying user files.
///
/// This command intentionally mirrors installer detection rules so users can
/// inspect integration readiness before manually changing Niri or Noctalia.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    var json_mode = false;
    var i: usize = 2;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        try writeErrorJson(io, "doctor", "ERR_CLI_USAGE", "usage: shaula doctor [--json]");
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var report = try collectReport(allocator, io, environ);
    defer report.deinit(allocator);

    if (json_mode) {
        try writeJson(allocator, io, report);
    } else {
        try writeHuman(io, report);
    }
    return 0;
}

fn collectReport(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !DoctorReport {
    const binary_path = std.process.executablePathAlloc(io, allocator) catch null;
    const xdg_config_dir = try resolveXdgConfigDir(allocator, environ);
    const config_file_path = try loader.resolveConfigPath(allocator, environ);
    errdefer if (config_file_path) |path| allocator.free(path);
    const generated_dir_path = if (xdg_config_dir) |dir| try std.fmt.allocPrint(allocator, "{s}/shaula/generated", .{dir}) else null;
    const niri_snippet_path = if (generated_dir_path) |dir| try std.fmt.allocPrint(allocator, "{s}/niri-shaula.kdl", .{dir}) else null;

    var tools = try allocator.alloc(ToolStatus, tool_names.len);
    errdefer allocator.free(tools);
    for (tool_names, 0..) |name, index| {
        tools[index] = .{ .name = name, .path = try findToolInPath(allocator, io, environ, name) };
    }

    const niri_config_env = envSlice(environ, "NIRI_CONFIG");
    const wayland_display = envSlice(environ, "WAYLAND_DISPLAY");
    const xdg_session_type = envSlice(environ, "XDG_SESSION_TYPE");
    const niri_user_config_path = try homePath(allocator, environ, ".config/niri/config.kdl");
    defer if (niri_user_config_path) |path| allocator.free(path);
    const niri_cfg_dir_path = try homePath(allocator, environ, ".config/niri/cfg");
    defer if (niri_cfg_dir_path) |path| allocator.free(path);
    const noctalia_dir_path = try homePath(allocator, environ, ".config/noctalia");
    defer if (noctalia_dir_path) |path| allocator.free(path);
    const noctalia_plugins_dir_path = try homePath(allocator, environ, ".config/noctalia/plugins");
    defer if (noctalia_plugins_dir_path) |path| allocator.free(path);
    const noctalia_plugins_json_path = try homePath(allocator, environ, ".config/noctalia/plugins.json");
    defer if (noctalia_plugins_json_path) |path| allocator.free(path);
    const noctalia_settings_json_path = try homePath(allocator, environ, ".config/noctalia/settings.json");
    defer if (noctalia_settings_json_path) |path| allocator.free(path);
    const noctalia_shaula_plugin_dir_path = try homePath(allocator, environ, ".config/noctalia/plugins/shaula");
    defer if (noctalia_shaula_plugin_dir_path) |path| allocator.free(path);

    var warnings = std.ArrayList([]const u8).empty;
    errdefer warnings.deinit(allocator);
    if (!toolFound(tools, "grim")) try warnings.append(allocator, "missing grim");
    if (!toolFound(tools, "wl-copy")) try warnings.append(allocator, "missing wl-copy");
    if (wayland_display == null or wayland_display.?.len == 0) try warnings.append(allocator, "missing WAYLAND_DISPLAY");
    if (xdg_session_type == null or !std.mem.eql(u8, xdg_session_type.?, "wayland")) try warnings.append(allocator, "XDG_SESSION_TYPE is not wayland");

    const niri_detected =
        toolFound(tools, "niri") or
        (niri_config_env != null and niri_config_env.?.len > 0) or
        (niri_user_config_path != null and fileExists(io, niri_user_config_path.?)) or
        (niri_cfg_dir_path != null and fileExists(io, niri_cfg_dir_path.?)) or
        fileExists(io, "/etc/niri/config.kdl");
    const niri_snippet_exists = niri_snippet_path != null and fileExists(io, niri_snippet_path.?);
    if (niri_detected and !niri_snippet_exists) try warnings.append(allocator, "Niri detected but generated snippet is missing");

    const noctalia_detected =
        (noctalia_dir_path != null and fileExists(io, noctalia_dir_path.?)) or
        (noctalia_plugins_dir_path != null and fileExists(io, noctalia_plugins_dir_path.?)) or
        (noctalia_plugins_json_path != null and fileExists(io, noctalia_plugins_json_path.?)) or
        (noctalia_settings_json_path != null and fileExists(io, noctalia_settings_json_path.?));
    const noctalia_shaula_plugin_dir_exists = noctalia_shaula_plugin_dir_path != null and fileExists(io, noctalia_shaula_plugin_dir_path.?);
    const noctalia_shaula_plugin_enabled = if (noctalia_plugins_json_path) |path| try detectNoctaliaPluginEnabled(allocator, io, path) else null;
    if (noctalia_detected and !noctalia_shaula_plugin_dir_exists) try warnings.append(allocator, "Noctalia detected but Shaula plugin is not installed");

    return .{
        .binary_path = binary_path,
        .xdg_config_dir = xdg_config_dir,
        .config_file_path = config_file_path,
        .config_exists = config_file_path != null and fileExists(io, config_file_path.?),
        .generated_dir_path = generated_dir_path,
        .niri_snippet_path = niri_snippet_path,
        .niri_snippet_exists = niri_snippet_exists,
        .xdg_session_type = xdg_session_type,
        .wayland_display = wayland_display,
        .niri_config_env = niri_config_env,
        .niri_config_env_exists = niri_config_env != null and niri_config_env.?.len > 0 and fileExists(io, niri_config_env.?),
        .niri_user_config_exists = niri_user_config_path != null and fileExists(io, niri_user_config_path.?),
        .niri_cfg_dir_exists = niri_cfg_dir_path != null and fileExists(io, niri_cfg_dir_path.?),
        .niri_etc_config_exists = fileExists(io, "/etc/niri/config.kdl"),
        .noctalia_dir_exists = noctalia_dir_path != null and fileExists(io, noctalia_dir_path.?),
        .noctalia_plugins_dir_exists = noctalia_plugins_dir_path != null and fileExists(io, noctalia_plugins_dir_path.?),
        .noctalia_plugins_json_exists = noctalia_plugins_json_path != null and fileExists(io, noctalia_plugins_json_path.?),
        .noctalia_settings_json_exists = noctalia_settings_json_path != null and fileExists(io, noctalia_settings_json_path.?),
        .noctalia_shaula_plugin_dir_exists = noctalia_shaula_plugin_dir_exists,
        .noctalia_shaula_plugin_enabled = noctalia_shaula_plugin_enabled,
        .tools = tools,
        .warnings = try warnings.toOwnedSlice(allocator),
    };
}

const tool_names = [_][]const u8{ "grim", "slurp", "wl-copy", "wl-paste", "niri", "quickshell" };

fn writeHuman(io: std.Io, report: DoctorReport) !void {
    var buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.writeAll("Shaula doctor\n\n");
    try stdout.interface.print("Paths:\n  binary: {s}\n  XDG config dir: {s}\n  config.toml: {s} ({s})\n  generated dir: {s}\n  Niri snippet: {s} ({s})\n\n", .{
        report.binary_path orelse "unknown",
        report.xdg_config_dir orelse "unknown",
        report.config_file_path orelse "unknown",
        if (report.config_exists) "exists" else "missing",
        report.generated_dir_path orelse "unknown",
        report.niri_snippet_path orelse "unknown",
        if (report.niri_snippet_exists) "exists" else "missing",
    });
    try stdout.interface.print("Wayland:\n  XDG_SESSION_TYPE: {s}\n  WAYLAND_DISPLAY: {s}\n\n", .{
        report.xdg_session_type orelse "unset",
        report.wayland_display orelse "unset",
    });
    try stdout.interface.print("Niri:\n  niri in PATH: {s}\n  NIRI_CONFIG: {s} ({s})\n  ~/.config/niri/config.kdl: {s}\n  ~/.config/niri/cfg/: {s}\n  /etc/niri/config.kdl: {s}\n\n", .{
        if (toolFound(report.tools, "niri")) "yes" else "no",
        report.niri_config_env orelse "unset",
        if (report.niri_config_env_exists) "exists" else "missing",
        if (report.niri_user_config_exists) "exists" else "missing",
        if (report.niri_cfg_dir_exists) "exists" else "missing",
        if (report.niri_etc_config_exists) "exists" else "missing",
    });
    try stdout.interface.print("Noctalia:\n  ~/.config/noctalia/: {s}\n  ~/.config/noctalia/plugins/: {s}\n  ~/.config/noctalia/plugins.json: {s}\n  ~/.config/noctalia/settings.json: {s}\n  ~/.config/noctalia/plugins/shaula/: {s}\n  Shaula plugin enabled: {s}\n\n", .{
        if (report.noctalia_dir_exists) "exists" else "missing",
        if (report.noctalia_plugins_dir_exists) "exists" else "missing",
        if (report.noctalia_plugins_json_exists) "exists" else "missing",
        if (report.noctalia_settings_json_exists) "exists" else "missing",
        if (report.noctalia_shaula_plugin_dir_exists) "exists" else "missing",
        optionalBoolText(report.noctalia_shaula_plugin_enabled),
    });
    try stdout.interface.writeAll("Runtime tools:\n");
    for (report.tools) |tool| {
        try stdout.interface.print("  {s}: {s}\n", .{ tool.name, tool.path orelse "missing" });
    }
    if (report.warnings.len > 0) {
        try stdout.interface.writeAll("\nWarnings:\n");
        for (report.warnings) |warning| try stdout.interface.print("  - {s}\n", .{warning});
    }
    try stdout.interface.flush();
}

fn writeJson(allocator: std.mem.Allocator, io: std.Io, report: DoctorReport) !void {
    const ts = try command_json.nowIso8601(allocator, io);
    defer allocator.free(ts);
    const ts_json = try command_json.jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const paths_json = try pathsJson(allocator, report);
    defer allocator.free(paths_json);
    const wayland_json = try waylandJson(allocator, report);
    defer allocator.free(wayland_json);
    const niri_json = try niriJson(allocator, report);
    defer allocator.free(niri_json);
    const noctalia_json = try noctaliaJson(allocator, report);
    defer allocator.free(noctalia_json);
    const tools_json = try toolsJson(allocator, report.tools);
    defer allocator.free(tools_json);
    const warnings_json = try command_json.warningsJson(allocator, report.warnings);
    defer allocator.free(warnings_json);

    var buffer: [32768]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"doctor\",\"timestamp\":{s},\"paths\":{s},\"wayland\":{s},\"niri\":{s},\"noctalia\":{s},\"tools\":{s},\"warnings\":{s}}}\n",
        .{ protocol.contract_version, ts_json, paths_json, wayland_json, niri_json, noctalia_json, tools_json, warnings_json },
    );
    try stdout.interface.flush();
}

fn pathsJson(allocator: std.mem.Allocator, report: DoctorReport) ![]u8 {
    const binary = try jsonNullable(allocator, report.binary_path);
    defer allocator.free(binary);
    const xdg = try jsonNullable(allocator, report.xdg_config_dir);
    defer allocator.free(xdg);
    const config = try jsonNullable(allocator, report.config_file_path);
    defer allocator.free(config);
    const generated = try jsonNullable(allocator, report.generated_dir_path);
    defer allocator.free(generated);
    const snippet = try jsonNullable(allocator, report.niri_snippet_path);
    defer allocator.free(snippet);
    return std.fmt.allocPrint(allocator, "{{\"binary\":{s},\"xdg_config_dir\":{s},\"config_file\":{s},\"config_exists\":{s},\"generated_dir\":{s},\"niri_snippet\":{s},\"niri_snippet_exists\":{s}}}", .{
        binary,
        xdg,
        config,
        boolJson(report.config_exists),
        generated,
        snippet,
        boolJson(report.niri_snippet_exists),
    });
}

fn waylandJson(allocator: std.mem.Allocator, report: DoctorReport) ![]u8 {
    const session = try jsonNullable(allocator, report.xdg_session_type);
    defer allocator.free(session);
    const display = try jsonNullable(allocator, report.wayland_display);
    defer allocator.free(display);
    return std.fmt.allocPrint(allocator, "{{\"xdg_session_type\":{s},\"wayland_display\":{s}}}", .{ session, display });
}

fn niriJson(allocator: std.mem.Allocator, report: DoctorReport) ![]u8 {
    const niri_config = try jsonNullable(allocator, report.niri_config_env);
    defer allocator.free(niri_config);
    return std.fmt.allocPrint(allocator, "{{\"in_path\":{s},\"niri_config_env\":{s},\"niri_config_env_exists\":{s},\"user_config_exists\":{s},\"cfg_dir_exists\":{s},\"etc_config_exists\":{s}}}", .{
        boolJson(toolFound(report.tools, "niri")),
        niri_config,
        boolJson(report.niri_config_env_exists),
        boolJson(report.niri_user_config_exists),
        boolJson(report.niri_cfg_dir_exists),
        boolJson(report.niri_etc_config_exists),
    });
}

fn noctaliaJson(allocator: std.mem.Allocator, report: DoctorReport) ![]u8 {
    return std.fmt.allocPrint(allocator, "{{\"dir_exists\":{s},\"plugins_dir_exists\":{s},\"plugins_json_exists\":{s},\"settings_json_exists\":{s},\"shaula_plugin_dir_exists\":{s},\"shaula_plugin_enabled\":{s},\"plugin_installed\":{s}}}", .{
        boolJson(report.noctalia_dir_exists),
        boolJson(report.noctalia_plugins_dir_exists),
        boolJson(report.noctalia_plugins_json_exists),
        boolJson(report.noctalia_settings_json_exists),
        boolJson(report.noctalia_shaula_plugin_dir_exists),
        optionalBoolJson(report.noctalia_shaula_plugin_enabled),
        boolJson(report.noctalia_shaula_plugin_dir_exists),
    });
}

fn toolsJson(allocator: std.mem.Allocator, tools: []const ToolStatus) ![]u8 {
    var out = std.ArrayList(u8).empty;
    errdefer out.deinit(allocator);
    try out.append(allocator, '{');
    for (tools, 0..) |tool, index| {
        if (index != 0) try out.append(allocator, ',');
        const name_json = try command_json.jsonStringAlloc(allocator, tool.name);
        defer allocator.free(name_json);
        const path_json = try jsonNullable(allocator, tool.path);
        defer allocator.free(path_json);
        const entry = try std.fmt.allocPrint(allocator, "{s}:{{\"found\":{s},\"path\":{s}}}", .{ name_json, boolJson(tool.found()), path_json });
        defer allocator.free(entry);
        try out.appendSlice(allocator, entry);
    }
    try out.append(allocator, '}');
    return out.toOwnedSlice(allocator);
}

fn resolveXdgConfigDir(allocator: std.mem.Allocator, environ: std.process.Environ) !?[]u8 {
    if (envSlice(environ, "XDG_CONFIG_HOME")) |value| {
        if (value.len > 0) return try allocator.dupe(u8, value);
    }
    if (envSlice(environ, "HOME")) |home| {
        if (home.len > 0) return try std.fmt.allocPrint(allocator, "{s}/.config", .{home});
    }
    return null;
}

fn homePath(allocator: std.mem.Allocator, environ: std.process.Environ, suffix: []const u8) !?[]u8 {
    if (envSlice(environ, "HOME")) |home| {
        if (home.len > 0) return try std.fmt.allocPrint(allocator, "{s}/{s}", .{ home, suffix });
    }
    return null;
}

fn findToolInPath(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, tool: []const u8) !?[]u8 {
    const path_env = envSlice(environ, "PATH") orelse return null;
    var parts = std.mem.splitScalar(u8, path_env, ':');
    while (parts.next()) |part| {
        if (part.len == 0) continue;
        const candidate = try std.fmt.allocPrint(allocator, "{s}/{s}", .{ part, tool });
        if (fileExists(io, candidate)) return candidate;
        allocator.free(candidate);
    }
    return null;
}

fn fileExists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
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

fn toolFound(tools: []const ToolStatus, name: []const u8) bool {
    for (tools) |tool| {
        if (std.mem.eql(u8, tool.name, name)) return tool.found();
    }
    return false;
}

fn envSlice(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    if (environ.getPosix(key)) |value| return std.mem.sliceTo(value, 0);
    return null;
}

fn jsonNullable(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return command_json.jsonStringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

fn boolJson(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn optionalBoolJson(value: ?bool) []const u8 {
    if (value) |unwrapped| return boolJson(unwrapped);
    return "null";
}

fn optionalBoolText(value: ?bool) []const u8 {
    if (value) |unwrapped| return if (unwrapped) "yes" else "no";
    return "unknown";
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    const ts = try command_json.nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_out = try command_json.jsonStringAlloc(allocator, command);
    defer allocator.free(command_out);
    const code_out = try command_json.jsonStringAlloc(allocator, code);
    defer allocator.free(code_out);
    const message_out = try command_json.jsonStringAlloc(allocator, message);
    defer allocator.free(message_out);
    var buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":\"{s}\",\"error\":{{\"code\":{s},\"message\":{s},\"retryable\":false,\"details\":{{}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_out, ts, code_out, message_out },
    );
    try stdout.interface.flush();
}
