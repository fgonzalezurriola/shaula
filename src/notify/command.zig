const std = @import("std");

const notify = @import("../notify.zig");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

const TestKind = enum {
    copied,
    saved,
    @"error",
};

/// Runs the notification smoke-test command through the same public notify API
/// used after preview exit.
///
/// Contract constraints:
/// - this is a Niri-friendly freedesktop notification probe, not Niri IPC.
/// - notification delivery failures are reported to stdout but remain non-fatal.
pub fn run(allocator: std.mem.Allocator, io: std.Io, argv: []const [*:0]const u8) !u8 {
    if (argv.len >= 4 and std.mem.eql(u8, argToSlice(argv[2]), "__saved-action-listener")) {
        const image_path = if (argv.len >= 5) argToSlice(argv[4]) else null;
        notify.runSavedNotificationActionListener(allocator, io, argToSlice(argv[3]), image_path) catch {};
        return 0;
    }
    if (argv.len >= 4 and std.mem.eql(u8, argToSlice(argv[2]), "reveal-file")) {
        notify.revealFileInManager(allocator, io, argToSlice(argv[3])) catch {};
        return 0;
    }

    if (argv.len < 3 or !std.mem.eql(u8, argToSlice(argv[2]), "test")) {
        try writeErrorJson(io, "notify test", "ERR_CLI_USAGE", "usage: shaula notify test [--kind copied|saved|error]", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    var kind: TestKind = .copied;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--kind")) {
            i += 1;
            if (i >= argv.len) {
                try writeErrorJson(io, "notify test", "ERR_CLI_USAGE", "--kind requires copied, saved, or error", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            kind = parseKind(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, "notify test", "ERR_CLI_USAGE", "--kind must be copied, saved, or error", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            continue;
        }
        try writeErrorJson(io, "notify test", "ERR_CLI_USAGE", "unsupported flag", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const delivered = sendTestNotification(allocator, io, kind);
    try writeResultJson(allocator, io, kind, delivered == null);
    return 0;
}

fn sendTestNotification(allocator: std.mem.Allocator, io: std.Io, kind: TestKind) ?anyerror {
    switch (kind) {
        .copied => notify.notifyScreenshotCopied(allocator, io) catch |err| return err,
        .saved => notify.notifyScreenshotSaved(allocator, io, "/tmp/shaula-notify-test.png") catch |err| return err,
        .@"error" => notify.notifyScreenshotCopyFailed(allocator, io, "Copy failed") catch |err| return err,
    }
    return null;
}

fn parseKind(value: []const u8) ?TestKind {
    if (std.mem.eql(u8, value, "copied")) return .copied;
    if (std.mem.eql(u8, value, "saved")) return .saved;
    if (std.mem.eql(u8, value, "error")) return .@"error";
    return null;
}

fn kindString(kind: TestKind) []const u8 {
    return switch (kind) {
        .copied => "copied",
        .saved => "saved",
        .@"error" => "error",
    };
}

fn writeResultJson(allocator: std.mem.Allocator, io: std.Io, kind: TestKind, ok: bool) !void {
    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const kind_json = try jsonStringAlloc(allocator, kindString(kind));
    defer allocator.free(kind_json);

    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":{s},\"contract_version\":\"{s}\",\"command\":\"notify test\",\"timestamp\":{s},\"result\":{{\"kind\":{s},\"delivered\":{s}}},\"warnings\":[]}}\n",
        .{ if (ok) "true" else "false", jsonContractVersion(), ts_json, kind_json, if (ok) "true" else "false" },
    );
    try stdout.interface.flush();
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

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_string_escape(jsonSpan(value), &output);
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
