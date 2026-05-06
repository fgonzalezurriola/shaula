const std = @import("std");

const protocol = @import("../ipc/protocol.zig");

pub const EmptyDetails = struct {};

/// Shared CLI JSON contract helpers.
///
/// Contract constraints: timestamps, string escaping, warnings arrays, and
/// basic `ERR_*` envelopes stay centralized so command families do not drift
/// from the AGENT-FIRST CLI contract.
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

pub fn stringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
}

pub fn nullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return stringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

pub fn nullableU32Alloc(allocator: std.mem.Allocator, value: ?u32) ![]u8 {
    if (value) |resolved| return std.fmt.allocPrint(allocator, "{d}", .{resolved});
    return allocator.dupe(u8, "null");
}

pub fn nullableI32Alloc(allocator: std.mem.Allocator, value: ?i32) ![]u8 {
    if (value) |resolved| return std.fmt.allocPrint(allocator, "{d}", .{resolved});
    return allocator.dupe(u8, "null");
}

pub fn warningsAlloc(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    if (warnings.len == 0) return allocator.dupe(u8, "[]");

    var list = std.ArrayList(u8).empty;
    defer list.deinit(allocator);

    try list.append(allocator, '[');
    for (warnings, 0..) |warning, index| {
        if (index != 0) try list.append(allocator, ',');
        const warning_json = try stringAlloc(allocator, warning);
        defer allocator.free(warning_json);
        try list.appendSlice(allocator, warning_json);
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

pub fn writeBasicError(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
) !void {
    try writeErrorWithDetails(io, command, code, message, retryable, "{}");
}

pub fn writeErrorWithDetails(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    details_json: []const u8,
) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try stringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try stringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const code_json = try stringAlloc(allocator, code);
    defer allocator.free(code_json);
    const message_json = try stringAlloc(allocator, message);
    defer allocator.free(message_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"error\":{{\"code\":{s},\"message\":{s},\"retryable\":{s},\"details\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            code_json,
            message_json,
            if (retryable) "true" else "false",
            details_json,
        },
    );
    try stdout.interface.flush();
}
