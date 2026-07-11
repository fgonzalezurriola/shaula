const std = @import("std");
const backend_contract = @import("../capture/backends/capture_backend_contract.zig");
const compositor_runtime = @import("../compositor/runtime.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

pub const PreflightError = error{UnsupportedCompositor};

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const runtime = runtime_capabilities.resolve(allocator, io, environ);
    if (!runtime.compositor_supported) {
        try writeErrorJson(io, "preflight", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const wayland_ready = environ.getPosix("WAYLAND_DISPLAY") != null;
    if (!wayland_ready) {
        try writeErrorJson(io, "preflight", "ERR_PREFLIGHT_ENV_NOT_READY", "wayland environment is not ready", true, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_PREFLIGHT_ENV_NOT_READY");
    }

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    const warning = runtime.usesPortalBackend();
    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"preflight\",\"timestamp\":\"{s}\",\"compositor\":\"{s}\",\"ready\":true,\"result\":{{\"compositor\":\"{s}\",\"wayland\":true,\"backend\":\"{s}\",\"portal_available\":{s}}},\"warnings\":{s}}}\n",
        .{
            jsonContractVersion(),
            ts,
            runtime.compositor.label,
            runtime.compositor.label,
            runtime.backendUsedLabel(),
            if (runtime.portal_available) "true" else "false",
            if (warning) "[\"" ++ backend_contract.warning_portal_fallback ++ "\"]" else "[]",
        },
    );
    try stdout.interface.flush();
    return 0;
}

pub fn detectCompositor(environ: std.process.Environ) []const u8 {
    return compositor_runtime.detect(environ).label;
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

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool, detected_compositor: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    const compositor_json = try jsonStringAlloc(allocator, detected_compositor);
    defer allocator.free(compositor_json);
    const details_json = try std.fmt.allocPrint(allocator, "{{\"detected_compositor\":{s}}}", .{compositor_json});
    defer allocator.free(details_json);

    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_basic_error_build(
        std.Io.Timestamp.now(io, .real).toSeconds(),
        jsonSpan(command),
        jsonSpan(code),
        jsonSpan(message),
        @intFromBool(retryable),
        jsonSpan(details_json),
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}

test "detect compositor is niri when SHAULA_COMPOSITOR is niri" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("SHAULA_COMPOSITOR", "niri");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("niri", detectCompositor(environ));
}

test "detect compositor preserves explicit runtime label" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("XDG_CURRENT_DESKTOP", "sway");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("sway", detectCompositor(environ));
}
