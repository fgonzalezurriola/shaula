const std = @import("std");

const cli_json = @import("../cli/json.zig");
const command_json = @import("../capture/command_json.zig");
const diagnostics = @import("diagnostics.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

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

    var report = try diagnostics.collect(allocator, io, environ);
    defer report.deinit(allocator);

    if (json_mode) {
        try writeJson(allocator, io, report);
    } else {
        try writeHuman(io, report);
    }
    return 0;
}

fn writeHuman(io: std.Io, report: diagnostics.Report) !void {
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
    try stdout.interface.print("Wayland:\n  XDG_SESSION_TYPE: {s}\n  WAYLAND_DISPLAY: {s}\n  compositor: {s}\n  active backend: {s}\n  portal available: {s}\n  portal window capable: {s}\n  overlay supported: {s}\n\n", .{
        report.xdg_session_type orelse "unset",
        report.wayland_display orelse "unset",
        report.detected_compositor,
        report.backend_active,
        if (report.portal_available) "yes" else "no",
        if (report.portal_window_capable) "yes" else "no",
        if (report.overlay_supported) "yes" else "no",
    });
    try stdout.interface.print("Niri:\n  niri in PATH: {s}\n  NIRI_CONFIG: {s} ({s})\n  ~/.config/niri/config.kdl: {s}\n  ~/.config/niri/cfg/: {s}\n  /etc/niri/config.kdl: {s}\n\n", .{
        if (diagnostics.toolFound(report.tools, "niri")) "yes" else "no",
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

fn writeJson(allocator: std.mem.Allocator, io: std.Io, report: diagnostics.Report) !void {
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

fn pathsJson(allocator: std.mem.Allocator, report: diagnostics.Report) ![]u8 {
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

fn waylandJson(allocator: std.mem.Allocator, report: diagnostics.Report) ![]u8 {
    const session = try jsonNullable(allocator, report.xdg_session_type);
    defer allocator.free(session);
    const display = try jsonNullable(allocator, report.wayland_display);
    defer allocator.free(display);
    const compositor = try jsonNullable(allocator, report.detected_compositor);
    defer allocator.free(compositor);
    const backend = try jsonNullable(allocator, report.backend_active);
    defer allocator.free(backend);
    return std.fmt.allocPrint(allocator, "{{\"xdg_session_type\":{s},\"wayland_display\":{s},\"compositor\":{s},\"backend_active\":{s},\"portal_available\":{s},\"portal_window_capable\":{s},\"overlay_supported\":{s}}}", .{
        session,
        display,
        compositor,
        backend,
        boolJson(report.portal_available),
        boolJson(report.portal_window_capable),
        boolJson(report.overlay_supported),
    });
}

fn niriJson(allocator: std.mem.Allocator, report: diagnostics.Report) ![]u8 {
    const niri_config = try jsonNullable(allocator, report.niri_config_env);
    defer allocator.free(niri_config);
    return std.fmt.allocPrint(allocator, "{{\"in_path\":{s},\"niri_config_env\":{s},\"niri_config_env_exists\":{s},\"user_config_exists\":{s},\"cfg_dir_exists\":{s},\"etc_config_exists\":{s}}}", .{
        boolJson(diagnostics.toolFound(report.tools, "niri")),
        niri_config,
        boolJson(report.niri_config_env_exists),
        boolJson(report.niri_user_config_exists),
        boolJson(report.niri_cfg_dir_exists),
        boolJson(report.niri_etc_config_exists),
    });
}

fn noctaliaJson(allocator: std.mem.Allocator, report: diagnostics.Report) ![]u8 {
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

fn toolsJson(allocator: std.mem.Allocator, tools: []const diagnostics.ToolStatus) ![]u8 {
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
    try cli_json.writeBasicError(io, command, code, message, false);
}
