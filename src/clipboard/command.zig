const std = @import("std");

const clipboard_service = @import("service.zig");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "clipboard", "ERR_CLI_USAGE", "usage: shaula clipboard <copy-image|import-image> --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    if (!std.mem.eql(u8, subcommand, "import-image") and !std.mem.eql(u8, subcommand, "copy-image")) {
        try writeErrorJson(io, "clipboard", "ERR_CLI_USAGE", "unsupported clipboard subcommand", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var json_mode = false;
    var output: ?[]const u8 = null;
    var input: ?[]const u8 = null;

    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "clipboard import-image", "ERR_CLI_USAGE", "--output requires a path", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, subcommand, "copy-image") and std.mem.eql(u8, arg, "--input")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "clipboard copy-image", "ERR_CLI_USAGE", "--input requires a path", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            input = argToSlice(argv[i]);
            continue;
        }
        try writeErrorJson(io, "clipboard import-image", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, "clipboard import-image", "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (std.mem.eql(u8, subcommand, "copy-image")) {
        const input_path = input orelse {
            try writeErrorJson(io, "clipboard copy-image", "ERR_CLI_USAGE", "--input is required", false);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        };
        return runCopyImage(allocator, io, environ, input_path);
    }

    const imported_path = clipboard_service.importImage(allocator, io, environ, output) catch |err| switch (err) {
        error.ClipboardUnavailable => {
            try writeErrorJson(io, "clipboard import-image", "ERR_CLIPBOARD_UNAVAILABLE", "clipboard backend is unavailable", false);
            return recovery_policy.exitCodeFor("ERR_CLIPBOARD_UNAVAILABLE");
        },
        error.ClipboardImportInvalid => {
            try writeErrorJson(io, "clipboard import-image", "ERR_CLIPBOARD_IMPORT_INVALID", "clipboard image import failed", false);
            return recovery_policy.exitCodeFor("ERR_CLIPBOARD_IMPORT_INVALID");
        },
        else => {
            try writeErrorJson(io, "clipboard import-image", "ERR_UNKNOWN_UNMAPPED", "clipboard image import failed with unmapped error", false);
            return recovery_policy.exitCodeFor("ERR_UNKNOWN_UNMAPPED");
        },
    };
    defer allocator.free(imported_path);

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"clipboard import-image\",\"timestamp\":\"{s}\",\"path\":\"{s}\",\"result\":{{\"path\":\"{s}\"}},\"warnings\":[]}}\n",
        .{ jsonContractVersion(), ts, imported_path, imported_path },
    );
    try stdout.interface.flush();
    return 0;
}

fn runCopyImage(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, input_path: []const u8) !u8 {
    const copy_result = clipboard_service.copyImage(io, environ, input_path) catch {
        try writeErrorJson(io, "clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED", "clipboard image copy failed with unmapped error", false);
        return recovery_policy.exitCodeFor("ERR_CLIPBOARD_COPY_FAILED");
    };

    if (!copy_result.ok) {
        const code = copy_result.code orelse "ERR_CLIPBOARD_COPY_FAILED";
        const message = copy_result.message orelse "clipboard image copy failed";
        try writeErrorJson(io, "clipboard copy-image", code, message, false);
        return recovery_policy.exitCodeFor(code);
    }

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"clipboard copy-image\",\"timestamp\":\"{s}\",\"result\":{{\"input\":\"{s}\",\"copied\":true}},\"warnings\":[]}}\n",
        .{ jsonContractVersion(), ts, input_path },
    );
    try stdout.interface.flush();
    return 0;
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

    var buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
