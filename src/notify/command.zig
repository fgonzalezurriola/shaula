const std = @import("std");

const notify = @import("../notify.zig");
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
    try writeResultJson(io, kind, delivered == null);
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

fn writeResultJson(io: std.Io, kind: TestKind, ok: bool) !void {
    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":{s},\"command\":\"notify test\",\"kind\":\"{s}\"}}\n",
        .{ if (ok) "true" else "false", kindString(kind) },
    );
    try stdout.interface.flush();
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"command\":\"{s}\",\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s}}}}}\n",
        .{ command, code, message, if (retryable) "true" else "false" },
    );
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
