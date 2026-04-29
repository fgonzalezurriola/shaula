const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

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

    std.Io.Dir.cwd().access(io, path, .{}) catch {
        try writeErrorJson(io, "preview", "ERR_PREVIEW_INPUT_INVALID", "preview input image is not readable", false);
        return recovery_policy.exitCodeFor("ERR_PREVIEW_INPUT_INVALID");
    };

    const helper_bin = try resolvePreviewBinary(allocator, io, environ);
    defer allocator.free(helper_bin);

    const result = std.process.run(allocator, io, .{
        .argv = &.{ helper_bin, path },
        .stdout_limit = .limited(2048),
        .stderr_limit = .limited(4096),
    }) catch {
        try writeErrorJson(io, "preview", "ERR_PREVIEW_UNAVAILABLE", "preview helper is unavailable", true);
        return recovery_policy.exitCodeFor("ERR_PREVIEW_UNAVAILABLE");
    };
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code == 43) {
                try writeErrorJson(io, "preview", "ERR_PREVIEW_INPUT_INVALID", "preview input image is invalid", false);
                return recovery_policy.exitCodeFor("ERR_PREVIEW_INPUT_INVALID");
            }
            if (code != 0) {
                try writeErrorJson(io, "preview", "ERR_PREVIEW_UNAVAILABLE", "preview helper exited unsuccessfully", true);
                return recovery_policy.exitCodeFor("ERR_PREVIEW_UNAVAILABLE");
            }
        },
        else => {
            try writeErrorJson(io, "preview", "ERR_PREVIEW_UNAVAILABLE", "preview helper terminated unexpectedly", true);
            return recovery_policy.exitCodeFor("ERR_PREVIEW_UNAVAILABLE");
        },
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"preview\",\"timestamp\":\"{s}\",\"result\":{{\"path\":\"{s}\",\"closed\":true}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts, path },
    );
    try stdout.interface.flush();
    return 0;
}

fn resolvePreviewBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_PREVIEW_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-preview");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-preview", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-preview");
    }
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
    if (m <= 0) m += 12;

    const hh = @divFloor(secs_of_day, 3600);
    const mm = @divFloor(@mod(secs_of_day, 3600), 60);
    const ss = @mod(secs_of_day, 60);

    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(y)),
        @as(u64, @intCast(m)),
        @as(u64, @intCast(d)),
        @as(u64, @intCast(hh)),
        @as(u64, @intCast(mm)),
        @as(u64, @intCast(ss)),
    });
}
