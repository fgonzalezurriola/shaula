const std = @import("std");

const output_path = @import("../backends/capture_backend_output_path.zig");
const cli_json = @import("../cli/json.zig");
const process_exec = @import("../runtime/process_exec.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

/// Resolve and optionally open user-facing Shaula directories.
///
/// Contract: `screenshots` mirrors the durable save directory used by explicit
/// captures, so shell integrations do not duplicate path fallback rules.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try cli_json.writeBasicError(io, "directory", "ERR_CLI_USAGE", "usage: shaula directory screenshots [--open] [--json]", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const target = argToSlice(argv[2]);
    if (!std.mem.eql(u8, target, "screenshots")) {
        try cli_json.writeBasicError(io, "directory", "ERR_CLI_USAGE", "unsupported directory target", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var json_mode = false;
    var open = false;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--open")) {
            open = true;
            continue;
        }
        try cli_json.writeBasicError(io, "directory screenshots", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const dir = output_path.resolveSavedOutputDir(allocator, io, environ) catch {
        try cli_json.writeBasicError(io, "directory screenshots", "ERR_OUTPUT_PATH_INVALID", "screenshot directory is not writable", false);
        return recovery_policy.exitCodeFor("ERR_OUTPUT_PATH_INVALID");
    };
    defer allocator.free(dir);

    if (open) {
        const result = process_exec.run(allocator, io, &.{ "xdg-open", dir }, 4096, 4096) catch {
            try cli_json.writeBasicError(io, "directory screenshots", "ERR_OUTPUT_PATH_INVALID", "could not open screenshot directory", false);
            return recovery_policy.exitCodeFor("ERR_OUTPUT_PATH_INVALID");
        };
        defer result.deinit(allocator);
        if (!result.exitedZero()) {
            try cli_json.writeBasicError(io, "directory screenshots", "ERR_OUTPUT_PATH_INVALID", "could not open screenshot directory", false);
            return recovery_policy.exitCodeFor("ERR_OUTPUT_PATH_INVALID");
        }
    }

    if (json_mode) {
        try writeDirectoryJson(allocator, io, dir, open);
    }

    return 0;
}

fn writeDirectoryJson(allocator: std.mem.Allocator, io: std.Io, path: []const u8, opened: bool) !void {
    const ts = try cli_json.nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try cli_json.stringAlloc(allocator, "directory screenshots");
    defer allocator.free(command_json);
    const ts_json = try cli_json.stringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try cli_json.stringAlloc(allocator, path);
    defer allocator.free(path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"target\":\"screenshots\",\"path\":{s},\"opened\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts_json, path_json, if (opened) "true" else "false" },
    );
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
