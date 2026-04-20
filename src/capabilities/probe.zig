const std = @import("std");
const root = @import("root");

const standalone_protocol = struct {
    pub const contract_version = "1.0.0";
    pub const ipc_version = "1.0.0";
};

const standalone_preflight = struct {
    pub fn detectCompositor(environ: std.process.Environ) []const u8 {
        if (environ.getPosix("SHAULA_COMPOSITOR")) |value| {
            const explicit = std.mem.sliceTo(value, 0);
            if (std.ascii.eqlIgnoreCase(explicit, "niri")) return "niri";
            return "unsupported";
        }

        if (environ.getPosix("NIRI_SOCKET") != null) {
            return "niri";
        }

        return "unsupported";
    }
};

const standalone_recovery_policy = struct {
    pub fn exitCodeFor(code: []const u8) u8 {
        if (std.mem.eql(u8, code, "ERR_UNSUPPORTED_COMPOSITOR")) return 10;
        return 99;
    }
};

const protocol = if (@hasDecl(root, "protocol_module"))
    root.protocol_module
else
    standalone_protocol;

const preflight = if (@hasDecl(root, "preflight_probe_module"))
    root.preflight_probe_module
else
    standalone_preflight;

const runtime_capabilities = if (@hasDecl(root, "runtime_capabilities_module"))
    root.runtime_capabilities_module
else
    @import("runtime.zig");

const recovery_policy = if (@hasDecl(root, "recovery_policy_module"))
    root.recovery_policy_module
else
    standalone_recovery_policy;

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const compositor = preflight.detectCompositor(environ);
    if (!std.mem.eql(u8, compositor, "niri")) {
        try writeErrorJson(io, "capabilities list", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, compositor);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const runtime = runtime_capabilities.resolve(environ);
    const backend = runtime_capabilities.backendLabel(runtime.backend);
    const fallbacks = runtime_capabilities.fallbacksFor(runtime.backend);
    const fallbacks_json = try stringArrayJson(allocator, fallbacks);
    defer allocator.free(fallbacks_json);

    const window_warning = if (!runtime.capture.window) "window_capture_degraded" else null;
    const warnings = if (window_warning) |warning| blk: {
        const warning_json = try std.json.Stringify.valueAlloc(allocator, warning, .{});
        defer allocator.free(warning_json);
        break :blk try std.fmt.allocPrint(allocator, "[{s}]", .{warning_json});
    } else try allocator.dupe(u8, "[]");
    defer allocator.free(warnings);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"capabilities list\",\"timestamp\":\"{s}\",\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"result\":{{\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"compositor\":\"niri\",\"ipc_version\":\"{s}\"}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            ts,
            boolToJson(runtime.capture.area),
            boolToJson(runtime.capture.fullscreen),
            boolToJson(runtime.capture.window),
            backend,
            fallbacks_json,
            boolToJson(runtime.capture.area),
            boolToJson(runtime.capture.fullscreen),
            boolToJson(runtime.capture.window),
            backend,
            fallbacks_json,
            protocol.ipc_version,
            warnings,
        },
    );
    try stdout.interface.flush();
    return 0;
}

fn boolToJson(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn stringArrayJson(allocator: std.mem.Allocator, values: []const []const u8) ![]u8 {
    if (values.len == 0) {
        return allocator.dupe(u8, "[]");
    }

    var list = try std.ArrayList(u8).initCapacity(allocator, 32);
    defer list.deinit(allocator);

    try list.append(allocator, '[');
    for (values, 0..) |value, index| {
        if (index != 0) {
            try list.append(allocator, ',');
        }
        const encoded = try std.json.Stringify.valueAlloc(allocator, value, .{});
        defer allocator.free(encoded);
        try list.appendSlice(allocator, encoded);
    }
    try list.append(allocator, ']');

    return list.toOwnedSlice(allocator);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool, detected_compositor: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":\"{s}\",\"timestamp\":\"{s}\",\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s},\"details\":{{\"detected_compositor\":\"{s}\"}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command, ts, code, message, if (retryable) "true" else "false", detected_compositor },
    );
    try stdout.interface.flush();
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

test "capabilities compositor guard is deterministic" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "sway");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    try std.testing.expectEqualStrings("unsupported", preflight.detectCompositor(environ));
}

test "runtime capabilities backend naming is canonical" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    const runtime = runtime_capabilities.resolve(environ);
    try std.testing.expectEqualStrings("niri-wayland-direct", runtime_capabilities.backendLabel(runtime.backend));
    try std.testing.expect(!runtime.capture.window);
}
