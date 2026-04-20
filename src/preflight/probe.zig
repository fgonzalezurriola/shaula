const std = @import("std");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

pub const PreflightError = error{UnsupportedCompositor};

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, socket_arg: ?[]const u8) !u8 {
    const compositor = detectCompositor(environ);
    if (!std.mem.eql(u8, compositor, "niri")) {
        try writeErrorJson(io, "preflight", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, compositor);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const socket_path = try protocol.resolveSocketPath(allocator, environ, socket_arg);
    defer allocator.free(socket_path);

    const wayland_ready = environ.getPosix("WAYLAND_DISPLAY") != null;
    if (!wayland_ready) {
        try writeErrorJson(io, "preflight", "ERR_PREFLIGHT_ENV_NOT_READY", "wayland environment is not ready", true, compositor);
        return recovery_policy.exitCodeFor("ERR_PREFLIGHT_ENV_NOT_READY");
    }

    const ipc_ready = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"preflight\",\"timestamp\":\"{s}\",\"compositor\":\"niri\",\"ready\":true,\"ipc\":{{\"socket\":\"{s}\",\"ready\":{s}}},\"result\":{{\"compositor\":\"niri\",\"wayland\":true,\"ipc_ready\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts, socket_path, if (ipc_ready) "true" else "false", if (ipc_ready) "true" else "false" },
    );
    try stdout.interface.flush();
    return 0;
}

pub fn detectCompositor(environ: std.process.Environ) []const u8 {
    if (environ.getPosix("SHAULA_COMPOSITOR")) |value| {
        const explicit = std.mem.sliceTo(value, 0);
        if (std.ascii.eqlIgnoreCase(explicit, "niri")) return "niri";
        return "unsupported";
    }

    if (environ.getPosix("NIRI_SOCKET") != null) {
        return "niri";
    }

    if (environ.getPosix("XDG_CURRENT_DESKTOP")) |value| {
        if (containsDesktopToken(std.mem.sliceTo(value, 0), "niri")) return "niri";
    }

    if (environ.getPosix("XDG_SESSION_DESKTOP")) |value| {
        if (std.ascii.eqlIgnoreCase(std.mem.sliceTo(value, 0), "niri")) return "niri";
    }

    return "unsupported";
}

fn containsDesktopToken(value: []const u8, token: []const u8) bool {
    var it_colon = std.mem.splitScalar(u8, value, ':');
    while (it_colon.next()) |chunk| {
        var it_semicolon = std.mem.splitScalar(u8, chunk, ';');
        while (it_semicolon.next()) |subchunk| {
            if (std.ascii.eqlIgnoreCase(std.mem.trim(u8, subchunk, " \t"), token)) {
                return true;
            }
        }
    }
    return false;
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

test "detect compositor is niri when SHAULA_COMPOSITOR is niri" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("SHAULA_COMPOSITOR", "niri");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("niri", detectCompositor(environ));
}

test "detect compositor unsupported on non-niri marker" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("XDG_CURRENT_DESKTOP", "sway");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("unsupported", detectCompositor(environ));
}
