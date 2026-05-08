const std = @import("std");
const cli_json = @import("../cli/json.zig");
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
    try cli_json.writeBasicError(io, command, code, message, retryable);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}
