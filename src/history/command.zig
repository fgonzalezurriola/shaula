const std = @import("std");

const cli_json = @import("../cli/json.zig");
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
    return cli_json.stringAlloc(allocator, value);
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    try cli_json.writeBasicError(io, command, code, message, retryable);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}

test "history json helper escapes quoted paths" {
    const encoded = try jsonStringAlloc(std.testing.allocator, "/tmp/shaula/h\"ist\".png");
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("\"/tmp/shaula/h\\\"ist\\\".png\"", encoded);
}
