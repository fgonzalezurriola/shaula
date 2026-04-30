const std = @import("std");

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
        "{{\"preview\":{{\"window\":{{\"app_id\":{s},\"title\":{s},\"mode\":{s},\"focused\":{s},\"width\":{s},\"height\":{s},\"default_column_display\":{s},\"floating_position\":{{\"x\":{s},\"y\":{s},\"relative_to\":{s}}}}}}}}}",
        .{
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
    if (value) |resolved| return std.fmt.allocPrint(allocator, "{d}", .{resolved});
    return allocator.dupe(u8, "null");
}

fn nullableI32Json(allocator: std.mem.Allocator, value: ?i32) ![]u8 {
    if (value) |resolved| return std.fmt.allocPrint(allocator, "{d}", .{resolved});
    return allocator.dupe(u8, "null");
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

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const code_json = try jsonStringAlloc(allocator, code);
    defer allocator.free(code_json);
    const message_json = try jsonStringAlloc(allocator, message);
    defer allocator.free(message_json);
    const path_json = try jsonNullableStringAlloc(allocator, path);
    defer allocator.free(path_json);
    const field_json = try jsonNullableStringAlloc(allocator, field);
    defer allocator.free(field_json);
    const line_json = if (line) |value| try std.fmt.allocPrint(allocator, "{d}", .{value}) else try allocator.dupe(u8, "null");
    defer allocator.free(line_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":\"{s}\",\"error\":{{\"code\":{s},\"message\":{s},\"retryable\":{s},\"details\":{{\"path\":{s},\"line\":{s},\"field\":{s}}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts, code_json, message_json, if (retryable) "true" else "false", path_json, line_json, field_json },
    );
    try stdout.interface.flush();
}

fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    if (warnings.len == 0) return allocator.dupe(u8, "[]");
    var list = std.ArrayList(u8).empty;
    defer list.deinit(allocator);
    try list.append(allocator, '[');
    for (warnings, 0..) |warning, index| {
        if (index != 0) try list.append(allocator, ',');
        const warning_json = try jsonStringAlloc(allocator, warning);
        defer allocator.free(warning_json);
        try list.appendSlice(allocator, warning_json);
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return jsonStringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    const ts = std.Io.Timestamp.now(io, .real);
    const epoch_seconds: i64 = ts.toSeconds();

    const days: i64 = @divFloor(epoch_seconds, 86400);
    const secs_of_day: i64 = @mod(epoch_seconds, 86400);

    const z = days + 719468;
    const era = @divFloor(if (z >= 0) z else z - 146096, 146097);
    const doe = z - era * 146097;
    const yoe = @divFloor(doe - @divFloor(doe, 1460) + @divFloor(doe, 36524) - @divFloor(doe, 146096), 365);
    var y = yoe + era * 400;
    const doy = doe - (365 * yoe + @divFloor(yoe, 4) - @divFloor(yoe, 100));
    const mp = @divFloor(5 * doy + 2, 153);
    const d = doy - @divFloor(153 * mp + 2, 5) + 1;
    var m: i64 = mp + (if (mp < 10) @as(i64, 3) else @as(i64, -9));
    y += if (m <= 2) 1 else 0;
    if (m <= 0) m += 12;

    const hh = @divFloor(secs_of_day, 3600);
    const mm = @divFloor(@mod(secs_of_day, 3600), 60);
    const ss = @mod(secs_of_day, 60);

    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(y)),
        @as(u64, @intCast(m)),
        @as(u64, @intCast(d)),
        @as(u64, @intCast(hh)),
        @as(u64, @intCast(mm)),
        @as(u64, @intCast(ss)),
    });
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
