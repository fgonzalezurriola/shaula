const std = @import("std");
const protocol = @import("../ipc/protocol.zig");
const taxonomy = @import("taxonomy.zig");
const recovery_policy = @import("../recovery/policy.zig");

pub fn run(allocator: std.mem.Allocator, io: std.Io, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "errors", "ERR_CLI_USAGE", "usage: shaula errors list --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    if (!std.mem.eql(u8, subcommand, "list")) {
        try writeErrorJson(io, "errors", "ERR_CLI_USAGE", "unsupported errors subcommand", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var json_mode = false;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        try writeErrorJson(io, "errors list", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, "errors list", "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [16384]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"errors list\",\"timestamp\":\"{s}\",\"result\":{{\"errors\":[",
        .{ protocol.contract_version, ts },
    );

    const specs = taxonomy.list();
    for (specs, 0..) |spec, index| {
        if (index != 0) try stdout.interface.writeAll(",");
        try stdout.interface.print(
            "{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s},\"class\":\"{s}\",\"action\":\"{s}\",\"exit_code\":{d}}}",
            .{
                spec.code,
                spec.message,
                if (spec.retryable) "true" else "false",
                spec.class.asString(),
                spec.action.asString(),
                spec.exit_code,
            },
        );
    }

    try stdout.interface.writeAll("]},\"warnings\":[]}\n");
    try stdout.interface.flush();
    return 0;
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":\"{s}\",\"timestamp\":\"{s}\",\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s},\"details\":{{}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command, ts, code, message, if (retryable) "true" else "false" },
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
