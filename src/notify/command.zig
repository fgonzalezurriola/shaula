const std = @import("std");

const cli_json = @import("../cli/json.zig");
const notify = @import("../notify.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

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
    const ts = try cli_json.nowIso8601(allocator, io);
    defer allocator.free(ts);
    const ts_json = try cli_json.stringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const kind_json = try cli_json.stringAlloc(allocator, kindString(kind));
    defer allocator.free(kind_json);

    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":{s},\"contract_version\":\"{s}\",\"command\":\"notify test\",\"timestamp\":{s},\"result\":{{\"kind\":{s},\"delivered\":{s}}},\"warnings\":[]}}\n",
        .{ if (ok) "true" else "false", protocol.contract_version, ts_json, kind_json, if (ok) "true" else "false" },
    );
    try stdout.interface.flush();
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    try cli_json.writeBasicError(io, command, code, message, retryable);
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
