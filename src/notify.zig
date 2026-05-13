const std = @import("std");
const process_exec = @import("runtime/process_exec.zig");

pub const NotifyUrgency = enum {
    low,
    normal,
    critical,

    fn asNotifySendArg(self: NotifyUrgency) []const u8 {
        return switch (self) {
            .low => "low",
            .normal => "normal",
            .critical => "critical",
        };
    }
};

pub fn notifyScreenshotSaved(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const body = try std.fmt.allocPrint(allocator, "Saved to {s}", .{path});
    defer allocator.free(body);
    try notifyWithImage(allocator, io, "Shaula captured", body, path, .normal, 2500, true);
}

pub fn notifyScreenshotCopied(allocator: std.mem.Allocator, io: std.Io) !void {
    try notifyWithImage(allocator, io, "Shaula captured", "You can paste the image from the clipboard.", null, .normal, 2500, true);
}

pub fn notifyScreenshotCopiedImage(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    try notifyWithImage(allocator, io, "Shaula captured", "You can paste the image from the clipboard.", path, .normal, 2500, true);
}

pub fn notifyScreenshotSaveFailed(allocator: std.mem.Allocator, io: std.Io, message: []const u8) !void {
    try notify(allocator, io, "Could not save screenshot", message, .normal, 6000, false);
}

pub fn notifyScreenshotCopyFailed(allocator: std.mem.Allocator, io: std.Io, message: []const u8) !void {
    try notify(allocator, io, "Could not copy screenshot", message, .normal, 5000, false);
}

/// Runtime boundary for desktop notifications.
///
/// This intentionally uses the freedesktop-compatible `notify-send` fallback so
/// capture/preview success is never coupled to DBus client code. Callers must
/// treat failures here as non-fatal.
pub fn notify(
    allocator: std.mem.Allocator,
    io: std.Io,
    summary: []const u8,
    body: []const u8,
    urgency: NotifyUrgency,
    timeout_ms: u32,
    transient: bool,
) !void {
    const timeout = try std.fmt.allocPrint(allocator, "{d}", .{timeout_ms});
    defer allocator.free(timeout);

    var argv: [9][]const u8 = undefined;
    argv[0] = "notify-send";
    argv[1] = "--app-name=Shaula";
    argv[2] = "--urgency";
    argv[3] = urgency.asNotifySendArg();
    argv[4] = "--expire-time";
    argv[5] = timeout;
    var argv_len: usize = 6;
    if (transient) {
        argv[argv_len] = "--transient";
        argv_len += 1;
    }
    argv[argv_len] = summary;
    argv_len += 1;
    argv[argv_len] = body;
    argv_len += 1;

    const result = process_exec.run(allocator, io, argv[0..argv_len], 1024, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);

    if (result.term != .exited or result.term.exited != 0) return error.NotificationUnavailable;
}

pub fn notifyWithImage(
    allocator: std.mem.Allocator,
    io: std.Io,
    summary: []const u8,
    body: []const u8,
    image_path: ?[]const u8,
    urgency: NotifyUrgency,
    timeout_ms: u32,
    transient: bool,
) !void {
    const timeout = try std.fmt.allocPrint(allocator, "{d}", .{timeout_ms});
    defer allocator.free(timeout);
    const image_hint = if (image_path) |path| try std.fmt.allocPrint(allocator, "string:image-path:file://{s}", .{path}) else null;
    defer if (image_hint) |hint| allocator.free(hint);

    var argv: [12][]const u8 = undefined;
    argv[0] = "notify-send";
    argv[1] = "--app-name=Shaula";
    argv[2] = "--urgency";
    argv[3] = urgency.asNotifySendArg();
    argv[4] = "--expire-time";
    argv[5] = timeout;
    var argv_len: usize = 6;
    if (transient) {
        argv[argv_len] = "--transient";
        argv_len += 1;
    }
    if (image_hint) |hint| {
        argv[argv_len] = "--hint";
        argv_len += 1;
        argv[argv_len] = hint;
        argv_len += 1;
    }
    argv[argv_len] = summary;
    argv_len += 1;
    argv[argv_len] = body;
    argv_len += 1;

    const result = process_exec.run(allocator, io, argv[0..argv_len], 1024, 1024) catch return fallbackNotifyIcon(allocator, io, summary, body, image_path, urgency, timeout, transient);
    defer result.deinit(allocator);

    if (result.term == .exited and result.term.exited == 0) return;
    try fallbackNotifyIcon(allocator, io, summary, body, image_path, urgency, timeout, transient);
}

fn fallbackNotifyIcon(
    allocator: std.mem.Allocator,
    io: std.Io,
    summary: []const u8,
    body: []const u8,
    image_path: ?[]const u8,
    urgency: NotifyUrgency,
    timeout: []const u8,
    transient: bool,
) !void {
    var argv: [11][]const u8 = undefined;
    argv[0] = "notify-send";
    argv[1] = "--app-name=Shaula";
    argv[2] = "--urgency";
    argv[3] = urgency.asNotifySendArg();
    argv[4] = "--expire-time";
    argv[5] = timeout;
    var argv_len: usize = 6;
    if (transient) {
        argv[argv_len] = "--transient";
        argv_len += 1;
    }
    if (image_path) |path| {
        argv[argv_len] = "-i";
        argv_len += 1;
        argv[argv_len] = path;
        argv_len += 1;
    }
    argv[argv_len] = summary;
    argv_len += 1;
    argv[argv_len] = body;
    argv_len += 1;

    const result = process_exec.run(allocator, io, argv[0..argv_len], 1024, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);
    if (result.term != .exited or result.term.exited != 0) return error.NotificationUnavailable;
}
