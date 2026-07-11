const std = @import("std");

const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
    @cInclude("runtime/helper_resolution.h");
});

fn helperSpan(value: []const u8) c.ShaulaRuntimeHelperSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn helperOverride(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn resolveSiblingHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    env_var: []const u8,
    binary_name: []const u8,
) ![]u8 {
    const executable_dir = std.process.executableDirPathAlloc(io, allocator) catch null;
    defer if (executable_dir) |path| allocator.free(path);

    var owned: c.ShaulaRuntimeHelperOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_helper_owned_path_clear(&owned);
    const status = c.shaula_runtime_helper_resolve(
        helperOverride(environ, env_var),
        if (executable_dir) |path| helperSpan(path) else .{ .data = null, .length = 0 },
        helperSpan(binary_name),
        &owned,
    );
    return switch (status) {
        c.SHAULA_RUNTIME_HELPER_STATUS_OK => allocator.dupe(u8, owned.data[0..owned.length]),
        c.SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT => error.InvalidPath,
        c.SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        else => error.HelperResolutionFailed,
    };
}
const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

/// Launches the native GTK settings helper.
///
/// Contract constraints:
/// - Noctalia and users invoke `shaula settings`; GTK stays isolated in the
///   sibling helper binary.
/// - Agents can invoke `shaula settings --json` for read-only discovery of
///   Shaula's machine contracts without opening the GTK helper.
/// - helper resolution prefers `SHAULA_SETTINGS_HELPER_BIN`, then the installed
///   sibling `shaula-settings`, then PATH.
/// - spawn/wait failures map to deterministic `ERR_SETTINGS_UNAVAILABLE`.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len == 3 and std.mem.eql(u8, argToSlice(argv[2]), "--json")) {
        try writeDiscoveryJson(allocator, io);
        return 0;
    }

    if (argv.len > 2) {
        try writeBasicError(io, "settings", "ERR_CLI_USAGE", "usage: shaula settings [--json]", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const helper_bin = try resolveSettingsBinary(allocator, io, environ);
    defer allocator.free(helper_bin);
    const shaula_bin = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(shaula_bin);

    var child = std.process.spawn(io, .{
        .argv = &.{ helper_bin, shaula_bin },
        .stdin = .ignore,
        .stdout = .ignore,
        .stderr = .ignore,
    }) catch {
        try writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper is unavailable", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    };

    const term = child.wait(io) catch {
        try writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper is unavailable", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    };

    const ok = switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
    if (!ok) {
        try writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper failed", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    }

    return 0;
}

fn resolveSettingsBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    return resolveSiblingHelper(allocator, io, environ, "SHAULA_SETTINGS_HELPER_BIN", "shaula-settings");
}

fn writeDiscoveryJson(allocator: std.mem.Allocator, io: std.Io) !void {
    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"settings\",\"timestamp\":{s},\"result\":{{\"purpose\":\"shaula_agent_discovery\",\"human_ui\":\"shaula settings\",\"privacy\":{{\"explore_captures_pixels\":false,\"capture_captures_pixels\":true,\"window_titles_may_be_sensitive\":true,\"screenshots_stay_local_by_default\":true}},\"recommended_flow\":[\"shaula settings --json\",\"shaula doctor --json\",\"shaula capabilities list --json\",\"shaula explore --json --brief\",\"shaula explore --json\",\"shaula capture fullscreen --json --no-preview\"],\"commands\":{{\"discover\":\"shaula settings --json\",\"health\":\"shaula doctor --json\",\"capabilities\":\"shaula capabilities list --json\",\"desktop_inventory\":\"shaula explore --json [--brief]\",\"capture_current_output\":\"shaula capture fullscreen --json --no-preview\",\"capture_all_outputs\":\"shaula capture all-screens --json --no-preview\",\"capture_area\":\"shaula capture area --json --no-preview\",\"open_settings_ui\":\"shaula settings\"}},\"json_contract\":{{\"success_path\":\".result\",\"error_code_path\":\".error.code\",\"warnings_path\":\".warnings\",\"capture_path\":\".result.path // .path\",\"recommended_capture_path\":\".result.recommended_capture\"}}}},\"warnings\":[]}}\n",
        .{jsonContractVersion(), ts_json},
    );
    try stdout.interface.flush();
}

fn jsonSpan(value: []const u8) c.ShaulaJsonSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn jsonContractVersion() []const u8 {
    const value = c.shaula_json_contract_version();
    return value.data[0..value.length];
}

fn jsonTimestampAlloc(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_timestamp_from_unix_seconds(std.Io.Timestamp.now(io, .real).toSeconds(), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_string_escape(jsonSpan(value), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn writeBasicError(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_basic_error_build(
        std.Io.Timestamp.now(io, .real).toSeconds(),
        jsonSpan(command),
        jsonSpan(code),
        jsonSpan(message),
        @intFromBool(retryable),
        jsonSpan("{}"),
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;

    var buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
