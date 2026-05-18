const std = @import("std");

const cli_json = @import("../cli/json.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");
const preview_service = @import("service.zig");

/// Runs the interactive post-capture preview command.
///
/// Contract constraints:
/// - `--json` is required so automation receives a deterministic completion
///   envelope after the GTK preview window closes.
/// - missing helper/image paths map to stable preview-specific `ERR_*` tokens.
/// - the helper owns UI actions; this boundary only validates, launches, and
///   reports completion.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "preview", "ERR_CLI_USAGE", "usage: shaula preview <file> --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const path = argToSlice(argv[2]);
    var json_mode = false;

    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        try writeErrorJson(io, "preview", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, "preview", "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var outcome = try preview_service.runPreview(allocator, io, environ, path, false);
    switch (outcome) {
        .failure => |failure| {
            try writeErrorJson(io, "preview", failure.code, failure.message, failure.retryable);
            return recovery_policy.exitCodeFor(failure.code);
        },
        .success => |*result| {
            defer result.deinit(allocator);
            try writeSuccessJson(allocator, io, path, result.*);
            return 0;
        },
    }
}

fn writeSuccessJson(allocator: std.mem.Allocator, io: std.Io, path: []const u8, result: preview_service.PreviewRunResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const path_json = try jsonStringAlloc(allocator, path);
    defer allocator.free(path_json);
    const action_json = try jsonStringAlloc(allocator, result.action.asString());
    defer allocator.free(action_json);
    const saved_path_json = try jsonNullableStringAlloc(allocator, result.saved_path);
    defer allocator.free(saved_path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"preview\",\"timestamp\":\"{s}\",\"result\":{{\"path\":{s},\"closed\":{s},\"action\":{s},\"copied\":{s},\"saved\":{s},\"saved_path\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            ts,
            path_json,
            if (result.closed) "true" else "false",
            action_json,
            if (result.copied) "true" else "false",
            if (result.saved) "true" else "false",
            saved_path_json,
        },
    );
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    try cli_json.writeBasicError(io, command, code, message, retryable);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return cli_json.stringAlloc(allocator, value);
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    return cli_json.nullableStringAlloc(allocator, value);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}
