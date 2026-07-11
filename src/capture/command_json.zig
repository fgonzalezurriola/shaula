const std = @import("std");

const selection = @import("../selection/selection.zig");
const capture_types = @import("types.zig");
const warning_tokens = @import("warnings.zig");
const c = @cImport({
    @cInclude("cli/json.h");
});

pub const known_warning_capture_selection_portal = warning_tokens.selection_portal;

pub fn writeSuccessJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    warnings: []const []const u8,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    const mode_json = try jsonStringAlloc(allocator, reported_mode);
    defer allocator.free(mode_json);
    const path_json = try jsonStringAlloc(allocator, success.path);
    defer allocator.free(path_json);
    const mime_json = try jsonStringAlloc(allocator, success.mime);
    defer allocator.free(mime_json);
    const backend_json = try jsonStringAlloc(allocator, success.backend_used);
    defer allocator.free(backend_json);

    const warnings_json = try warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"degraded\":{s},\"result\":{{\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d}}},\"warnings\":{s}}}\n",
        .{
            jsonContractVersion(),
            command_json,
            ts_json,
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            if (success.degraded) "true" else "false",
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

pub fn writeAreaDryRunJson(allocator: std.mem.Allocator, io: std.Io, result: selection.SelectionResult) !void {
    try writeSelectionDryRunJson(allocator, io, "capture area", result);
}

pub fn writeSelectionDryRunJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, result: selection.SelectionResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);

    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    const mode_json = try jsonStringAlloc(allocator, @tagName(result.mode));
    defer allocator.free(mode_json);

    const aspect_json = try jsonNullableStringAlloc(allocator, result.aspect);
    defer allocator.free(aspect_json);

    const geometry_json = if (result.geometry) |g| blk: {
        break :blk try std.fmt.allocPrint(allocator, "{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}}", .{ g.x, g.y, g.width, g.height });
    } else try allocator.dupe(u8, "null");
    defer allocator.free(geometry_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"selection\":{{\"mode\":{s},\"aspect\":{s},\"geometry\":{s},\"cancelled\":false}},\"preview\":{{\"attempted\":false,\"ok\":false,\"error\":null}},\"warnings\":[]}}\n",
        .{ jsonContractVersion(), command_json, ts_json, mode_json, aspect_json, geometry_json },
    );
    try stdout.interface.flush();
}

pub fn writeErrorJson(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    mode: ?[]const u8,
    backend_used: ?[]const u8,
    degraded: bool,
    warnings: []const []const u8,
) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const code_json = try jsonStringAlloc(allocator, code);
    defer allocator.free(code_json);
    const message_json = try jsonStringAlloc(allocator, message);
    defer allocator.free(message_json);

    const mode_json = try jsonNullableStringAlloc(allocator, mode);
    defer allocator.free(mode_json);

    const backend_json = try jsonNullableStringAlloc(allocator, backend_used);
    defer allocator.free(backend_json);

    const warning_json = try warningsJson(allocator, warnings);
    defer allocator.free(warning_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"backend_used\":{s},\"degraded\":{s},\"error\":{{\"code\":{s},\"message\":{s},\"retryable\":{s},\"details\":{{\"mode\":{s}}}}},\"warnings\":{s}}}\n",
        .{
            jsonContractVersion(),
            command_json,
            ts_json,
            mode_json,
            backend_json,
            if (degraded) "true" else "false",
            code_json,
            message_json,
            if (retryable) "true" else "false",
            mode_json,
            warning_json,
        },
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

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_string_escape(jsonSpan(value), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_nullable_string_escape(
        @intFromBool(value != null),
        if (value) |text| jsonSpan(text) else .{ .data = null, .length = 0 },
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    const spans = try allocator.alloc(c.ShaulaJsonSpan, warnings.len);
    defer allocator.free(spans);
    for (warnings, 0..) |warning, index| spans[index] = jsonSpan(warning);

    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_warnings_serialize(if (spans.len == 0) null else spans.ptr, spans.len, &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_timestamp_from_unix_seconds(std.Io.Timestamp.now(io, .real).toSeconds(), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}
