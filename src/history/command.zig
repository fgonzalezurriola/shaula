const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const history_store = @import("store.zig");
const recovery_policy = @import("../recovery/policy.zig");

pub fn run(allocator: std.mem.Allocator, io: std.Io, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "history", "ERR_CLI_USAGE", "usage: shaula history <list|show> --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    if (!std.mem.eql(u8, subcommand, "list") and !std.mem.eql(u8, subcommand, "show")) {
        try writeErrorJson(io, "history", "ERR_CLI_USAGE", "unsupported history subcommand", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var json_mode = false;
    var id: ?[]const u8 = null;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, subcommand, "show") and std.mem.eql(u8, arg, "--id")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "history show", "ERR_CLI_USAGE", "--id requires an entry id", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            id = argToSlice(argv[i]);
            continue;
        }
        try writeErrorJson(io, "history list", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, "history list", "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (std.mem.eql(u8, subcommand, "show")) {
        const entry_id = id orelse {
            try writeErrorJson(io, "history show", "ERR_CLI_USAGE", "--id is required", false);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        };
        return runShow(allocator, io, entry_id);
    }

    return runList(allocator, io);
}

fn runList(allocator: std.mem.Allocator, io: std.Io) !u8 {
    const entries = try history_store.listEntries(allocator, io);
    defer history_store.deinitEntries(allocator, entries);

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"history list\",\"timestamp\":{s},\"result\":{{\"entries\":[",
        .{ protocol.contract_version, ts_json },
    );
    for (entries, 0..) |entry, idx| {
        if (idx != 0) try stdout.interface.print(",", .{});
        const path_json = try jsonStringAlloc(allocator, entry.path);
        defer allocator.free(path_json);
        const mime_json = try jsonStringAlloc(allocator, entry.mime);
        defer allocator.free(mime_json);
        const backend_json = try jsonStringAlloc(allocator, entry.backend_used);
        defer allocator.free(backend_json);
        const entry_ts_json = try jsonStringAlloc(allocator, entry.timestamp);
        defer allocator.free(entry_ts_json);
        try stdout.interface.print(
            "{{\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"timestamp\":{s}}}",
            .{ path_json, mime_json, entry.width, entry.height, backend_json, entry_ts_json },
        );
    }
    try stdout.interface.writeAll("]},\"warnings\":[]}\n");
    try stdout.interface.flush();
    return 0;
}

fn runShow(allocator: std.mem.Allocator, io: std.Io, entry_id: []const u8) !u8 {
    const entries = try history_store.listEntries(allocator, io);
    defer history_store.deinitEntries(allocator, entries);

    if (!std.mem.eql(u8, entry_id, "latest") or entries.len == 0) {
        try writeErrorJson(io, "history show", "ERR_HISTORY_ENTRY_NOT_FOUND", "history entry was not found", false);
        return recovery_policy.exitCodeFor("ERR_HISTORY_ENTRY_NOT_FOUND");
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    const entry = entries[0];
    const path_json = try jsonStringAlloc(allocator, entry.path);
    defer allocator.free(path_json);
    const mime_json = try jsonStringAlloc(allocator, entry.mime);
    defer allocator.free(mime_json);
    const backend_json = try jsonStringAlloc(allocator, entry.backend_used);
    defer allocator.free(backend_json);
    const entry_ts_json = try jsonStringAlloc(allocator, entry.timestamp);
    defer allocator.free(entry_ts_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"history show\",\"timestamp\":{s},\"result\":{{\"id\":\"latest\",\"entry\":{{\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"timestamp\":{s}}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts_json, path_json, mime_json, entry.width, entry.height, backend_json, entry_ts_json },
    );
    try stdout.interface.flush();
    return 0;
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
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

test "history json helper escapes quoted paths" {
    const encoded = try jsonStringAlloc(std.testing.allocator, "/tmp/shaula/h\"ist\".png");
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("\"/tmp/shaula/h\\\"ist\\\".png\"", encoded);
}
