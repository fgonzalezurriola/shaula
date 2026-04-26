const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const selection = @import("../selection/selection.zig");
const capture_types = @import("types.zig");

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
            protocol.contract_version,
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
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"selection\":{{\"mode\":{s},\"aspect\":{s},\"geometry\":{s},\"cancelled\":false}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts_json, mode_json, aspect_json, geometry_json },
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
            protocol.contract_version,
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

pub fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
}

pub fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return jsonStringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

pub fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    if (warnings.len == 0) return allocator.dupe(u8, "[]");

    var list = try std.ArrayList(u8).initCapacity(allocator, 32);
    defer list.deinit(allocator);

    try list.append(allocator, '[');
    for (warnings, 0..) |warning, index| {
        if (index != 0) {
            try list.append(allocator, ',');
        }
        try list.print(allocator, "\"{s}\"", .{warning});
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

pub fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
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

    const hh = @divFloor(secs_of_day, 3600);
    const mm = @divFloor(@mod(secs_of_day, 3600), 60);
    const ss = @mod(secs_of_day, 60);

    if (m <= 0) m += 12;

    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(y)),
        @as(u64, @intCast(m)),
        @as(u64, @intCast(d)),
        @as(u64, @intCast(hh)),
        @as(u64, @intCast(mm)),
        @as(u64, @intCast(ss)),
    });
}
