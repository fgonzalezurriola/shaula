const std = @import("std");

const cli_json = @import("../cli/json.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");
const config_types = @import("config.zig");
const loader = @import("loader.zig");
const niri_rule = @import("niri_rule.zig");
const manager = @import("manager.zig");

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "config", "ERR_CLI_USAGE", "usage: shaula config <show|init|niri-window-rule|niri-install> --json", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    const command = if (std.mem.eql(u8, subcommand, "show"))
        "config show"
    else if (std.mem.eql(u8, subcommand, "init"))
        "config init"
    else if (std.mem.eql(u8, subcommand, "niri-window-rule"))
        "config niri-window-rule"
    else if (std.mem.eql(u8, subcommand, "niri-install"))
        "config niri-install"
    else {
        try writeErrorJson(io, "config", "ERR_CLI_USAGE", "unsupported config subcommand", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    };

    var json_mode = false;
    var dry_run = false;
    var force = false;
    var path_override: ?[]const u8 = null;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--dry-run")) {
            dry_run = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--force")) {
            force = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--path")) {
            if (!std.mem.eql(u8, subcommand, "niri-install") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--path is supported only for niri-install and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            path_override = argToSlice(argv[i]);
            continue;
        }
        try writeErrorJson(io, command, "ERR_CLI_USAGE", "unsupported flag", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, command, "ERR_CLI_USAGE", "--json is required", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (std.mem.eql(u8, subcommand, "init")) {
        var result = manager.initConfig(allocator, io, environ, .{
            .overwrite = force,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeInitJson(allocator, io, command, result);
        return 0;
    }

    var loaded = loader.load(allocator, io, environ) catch |err| switch (err) {
        error.ConfigUnreadable => {
            const path = try loader.resolveConfigPath(allocator, environ);
            defer if (path) |value| allocator.free(value);
            try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration file is unreadable", false, path, null, null);
            return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
        },
        error.ConfigInvalid => {
            const path = try loader.resolveConfigPath(allocator, environ);
            defer if (path) |value| allocator.free(value);
            try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid configuration file", false, path, null, null);
            return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
        },
        else => return err,
    };
    defer loaded.deinit(allocator);

    if (std.mem.eql(u8, subcommand, "show")) {
        try writeShowJson(allocator, io, command, loaded);
        return 0;
    }

    var rendered = try niri_rule.renderPreviewWindowRule(allocator, loaded.config);
    defer rendered.deinit(allocator);
    if (std.mem.eql(u8, subcommand, "niri-install")) {
        var result = manager.installNiriRule(allocator, io, environ, rendered.kdl, .{
            .path_override = path_override,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigInvalid => {
                try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid Niri configuration managed block", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
            },
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeInstallJson(allocator, io, command, result, rendered.warnings);
        return 0;
    }
    try writeNiriRuleJson(allocator, io, command, loaded, rendered);
    return 0;
}

fn writeInitJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, result: manager.InitResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"created\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            if (result.created) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
        },
    );
    try stdout.interface.flush();
}

fn writeInstallJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    result: manager.InstallResult,
    warnings: []const []const u8,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);
    const backup_path_json = try jsonNullableStringAlloc(allocator, result.backup_path);
    defer allocator.free(backup_path_json);
    const warnings_json = try warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"backup_path\":{s},\"installed\":{s},\"replaced\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            backup_path_json,
            if (result.installed) "true" else "false",
            if (result.replaced) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

fn writeShowJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, loaded: loader.ConfigLoadResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonNullableStringAlloc(allocator, loaded.path);
    defer allocator.free(path_json);
    const config_json = try configJson(allocator, loaded.config);
    defer allocator.free(config_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"loaded\":{s},\"config\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts_json, path_json, if (loaded.loaded) "true" else "false", config_json },
    );
    try stdout.interface.flush();
}

fn writeNiriRuleJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, loaded: loader.ConfigLoadResult, rendered: niri_rule.RenderedRule) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonNullableStringAlloc(allocator, loaded.path);
    defer allocator.free(path_json);
    const app_id_json = try jsonStringAlloc(allocator, config_types.preview_app_id);
    defer allocator.free(app_id_json);
    const title_json = try jsonStringAlloc(allocator, config_types.preview_title);
    defer allocator.free(title_json);
    const kdl_json = try jsonStringAlloc(allocator, rendered.kdl);
    defer allocator.free(kdl_json);
    const warnings_json = try warningsJson(allocator, rendered.warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [16384]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"loaded\":{s},\"target\":\"preview\",\"app_id\":{s},\"title\":{s},\"kdl\":{s}}},\"warnings\":{s}}}\n",
        .{ protocol.contract_version, command_json, ts_json, path_json, if (loaded.loaded) "true" else "false", app_id_json, title_json, kdl_json, warnings_json },
    );
    try stdout.interface.flush();
}

fn configJson(allocator: std.mem.Allocator, config: config_types.Config) ![]u8 {
    const app_id_json = try jsonStringAlloc(allocator, config_types.preview_app_id);
    defer allocator.free(app_id_json);
    const title_json = try jsonStringAlloc(allocator, config_types.preview_title);
    defer allocator.free(title_json);
    const region_capture_mode_json = try jsonStringAlloc(allocator, config.capture.region_capture_mode.asString());
    defer allocator.free(region_capture_mode_json);
    const mode_json = try jsonStringAlloc(allocator, config.preview.window.mode.asString());
    defer allocator.free(mode_json);
    const display_json = try jsonStringAlloc(allocator, config.preview.window.default_column_display.asString());
    defer allocator.free(display_json);
    const relative_json = try jsonStringAlloc(allocator, config.preview.window.floating_position.relative_to.asString());
    defer allocator.free(relative_json);
    const width_json = try nullableU32Json(allocator, config.preview.window.width);
    defer allocator.free(width_json);
    const height_json = try nullableU32Json(allocator, config.preview.window.height);
    defer allocator.free(height_json);
    const x_json = try nullableI32Json(allocator, config.preview.window.floating_position.x);
    defer allocator.free(x_json);
    const y_json = try nullableI32Json(allocator, config.preview.window.floating_position.y);
    defer allocator.free(y_json);

    return std.fmt.allocPrint(
        allocator,
        "{{\"capture\":{{\"region_capture_mode\":{s}}},\"preview\":{{\"window\":{{\"app_id\":{s},\"title\":{s},\"mode\":{s},\"focused\":{s},\"width\":{s},\"height\":{s},\"default_column_display\":{s},\"floating_position\":{{\"x\":{s},\"y\":{s},\"relative_to\":{s}}}}}}}}}",
        .{
            region_capture_mode_json,
            app_id_json,
            title_json,
            mode_json,
            if (config.preview.window.focused) "true" else "false",
            width_json,
            height_json,
            display_json,
            x_json,
            y_json,
            relative_json,
        },
    );
}

fn nullableU32Json(allocator: std.mem.Allocator, value: ?u32) ![]u8 {
    return cli_json.nullableU32Alloc(allocator, value);
}

fn nullableI32Json(allocator: std.mem.Allocator, value: ?i32) ![]u8 {
    return cli_json.nullableI32Alloc(allocator, value);
}

fn writeErrorJson(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    path: ?[]const u8,
    line: ?u32,
    field: ?[]const u8,
) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const path_json = try jsonNullableStringAlloc(allocator, path);
    defer allocator.free(path_json);
    const field_json = try jsonNullableStringAlloc(allocator, field);
    defer allocator.free(field_json);
    const line_json = if (line) |value| try std.fmt.allocPrint(allocator, "{d}", .{value}) else try allocator.dupe(u8, "null");
    defer allocator.free(line_json);

    const details_json = try std.fmt.allocPrint(
        allocator,
        "{{\"path\":{s},\"line\":{s},\"field\":{s}}}",
        .{ path_json, line_json, field_json },
    );
    defer allocator.free(details_json);
    try cli_json.writeErrorWithDetails(io, command, code, message, retryable, details_json);
}

fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    return cli_json.warningsAlloc(allocator, warnings);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return cli_json.stringAlloc(allocator, value);
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    return cli_json.nullableStringAlloc(allocator, value);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
