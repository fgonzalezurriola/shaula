const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const clipboard_service = @import("service.zig");
const recovery_policy = @import("../recovery/policy.zig");

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

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"clipboard import-image\",\"timestamp\":\"{s}\",\"path\":\"{s}\",\"result\":{{\"path\":\"{s}\"}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts, imported_path, imported_path },
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

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"clipboard copy-image\",\"timestamp\":\"{s}\",\"result\":{{\"input\":\"{s}\",\"copied\":true}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts, input_path },
    );
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
