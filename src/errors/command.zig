const std = @import("std");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

fn requiredSpan(value: c.ShaulaErrorSpan) []const u8 {
    return value.data[0..value.length];
}

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

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [16384]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"errors list\",\"timestamp\":\"{s}\",\"result\":{{\"errors\":[",
        .{ jsonContractVersion(), ts },
    );

    const spec_count = c.shaula_error_taxonomy_count();
    var spec_index: usize = 0;
    while (spec_index < spec_count) : (spec_index += 1) {
        const spec_pointer = c.shaula_error_taxonomy_at(spec_index);
        if (spec_pointer == null) unreachable;
        const spec = spec_pointer[0];
        const class_token = c.shaula_failure_class_token(spec.failure_class);
        const action_token = c.shaula_recovery_action_token(spec.action);
        if (spec_index != 0) try stdout.interface.writeAll(",");
        try stdout.interface.print(
            "{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s},\"class\":\"{s}\",\"action\":\"{s}\",\"exit_code\":{d}}}",
            .{
                requiredSpan(spec.code),
                requiredSpan(spec.message),
                if (spec.retryable != 0) "true" else "false",
                requiredSpan(class_token),
                requiredSpan(action_token),
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

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_basic_error_build(
        std.Io.Timestamp.now(io, .real).toSeconds(),
        jsonSpan(command),
        jsonSpan(code),
        jsonSpan(message),
        @intFromBool(retryable),
        jsonSpan("{}"),
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}
