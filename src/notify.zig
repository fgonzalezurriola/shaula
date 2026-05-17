const std = @import("std");
const notify_request = @import("notify/request.zig");
const process_exec = @import("runtime/process_exec.zig");
const runtime_paths = @import("runtime/paths.zig");

const saved_screenshot_notification_timeout_ms: u32 = 6000;
const saved_screenshot_notification_body = "Saved to screenshots folder.";

pub const NotifyUrgency = notify_request.NotifyUrgency;

pub fn notifyScreenshotSaved(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const absolute_path = try absolutePathForNotification(allocator, io, path);
    defer allocator.free(absolute_path);
    spawnSavedNotificationActionListener(allocator, io, absolute_path, absolute_path) catch {
        try notifyWithImage(allocator, io, "Screenshot captured", saved_screenshot_notification_body, absolute_path, .normal, saved_screenshot_notification_timeout_ms, true);
    };
}

pub fn notifyScreenshotSavedText(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const absolute_path = try absolutePathForNotification(allocator, io, path);
    defer allocator.free(absolute_path);
    spawnSavedNotificationActionListener(allocator, io, absolute_path, null) catch {
        try notifyWithImage(allocator, io, "Screenshot captured", saved_screenshot_notification_body, null, .normal, saved_screenshot_notification_timeout_ms, true);
    };
}

pub fn notifyScreenshotCopied(allocator: std.mem.Allocator, io: std.Io) !void {
    try notifyWithImage(allocator, io, "Screenshot captured", "You can paste the image from the clipboard.", null, .normal, 2500, true);
}

pub fn notifyScreenshotCopiedImage(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const image_path: ?[]const u8 = if (runtime_paths.isRuntimeCaptureArtifact(path)) null else path;
    try notifyWithImage(allocator, io, "Screenshot captured", "You can paste the image from the clipboard.", image_path, .normal, 2500, true);
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
    var args = try notify_request.buildNotifySendArgs(allocator, .{
        .summary = summary,
        .body = body,
        .urgency = urgency,
        .timeout_ms = timeout_ms,
        .transient = transient,
    }, .hint);
    defer args.deinit(allocator);

    const result = process_exec.run(allocator, io, args.argv(), 1024, 1024) catch return error.NotificationUnavailable;
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
    const request = notify_request.NotificationRequest{
        .summary = summary,
        .body = body,
        .image_path = image_path,
        .urgency = urgency,
        .timeout_ms = timeout_ms,
        .transient = transient,
    };

    var args = try notify_request.buildNotifySendArgs(allocator, request, .hint);
    defer args.deinit(allocator);

    const result = process_exec.run(allocator, io, args.argv(), 1024, 1024) catch return fallbackNotifyIcon(allocator, io, request);
    defer result.deinit(allocator);

    if (result.term == .exited and result.term.exited == 0) return;
    try fallbackNotifyIcon(allocator, io, request);
}

fn fallbackNotifyIcon(
    allocator: std.mem.Allocator,
    io: std.Io,
    request: notify_request.NotificationRequest,
) !void {
    var args = try notify_request.buildNotifySendArgs(allocator, request, .icon);
    defer args.deinit(allocator);

    const result = process_exec.run(allocator, io, args.argv(), 1024, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);
    if (result.term != .exited or result.term.exited != 0) return error.NotificationUnavailable;
}

/// Shows the saved-screenshot notification and handles the freedesktop action.
///
/// Contract constraints:
/// - the freedesktop default action is always `default` with visible label
///   `Show in folder`;
/// - reveal failures are logged and non-fatal because capture already
///   succeeded;
/// - action handling stays in this shared notify path, not per capture mode.
pub fn runSavedNotificationActionListener(
    allocator: std.mem.Allocator,
    io: std.Io,
    path: []const u8,
    image_path: ?[]const u8,
) !void {
    const absolute_path = try absolutePathForNotification(allocator, io, path);
    defer allocator.free(absolute_path);

    const action = notifySavedWithAction(allocator, io, "Screenshot captured", saved_screenshot_notification_body, image_path, .normal, saved_screenshot_notification_timeout_ms, true) catch |err| {
        logRevealFailure("notify-action", absolute_path, err);
        return err;
    };
    defer allocator.free(action);

    if (std.mem.eql(u8, action, "default") or std.mem.eql(u8, action, "show-in-folder") or std.mem.eql(u8, action, "reveal-file")) {
        revealFileInManager(allocator, io, absolute_path) catch |err| {
            logRevealFailure("reveal-file", absolute_path, err);
        };
    }
}

pub fn revealFileInManager(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const absolute_path = try absolutePathForNotification(allocator, io, path);
    defer allocator.free(absolute_path);

    revealViaFileManager1(allocator, io, absolute_path) catch |dbus_err| {
        logRevealFailure("org.freedesktop.FileManager1.ShowItems", absolute_path, dbus_err);
        openParentDirectory(allocator, io, absolute_path) catch |xdg_err| {
            logRevealFailure("xdg-open parent directory", absolute_path, xdg_err);
            return xdg_err;
        };
    };
}

fn notifySavedWithAction(
    allocator: std.mem.Allocator,
    io: std.Io,
    summary: []const u8,
    body: []const u8,
    image_path: ?[]const u8,
    urgency: NotifyUrgency,
    timeout_ms: u32,
    transient: bool,
) ![]u8 {
    const request = notify_request.NotificationRequest{
        .summary = summary,
        .body = body,
        .image_path = image_path,
        .urgency = urgency,
        .timeout_ms = timeout_ms,
        .transient = transient,
        .action = .{ .id = "default", .label = "Show in folder" },
    };

    var args = try notify_request.buildNotifySendArgs(allocator, request, .hint);
    defer args.deinit(allocator);

    const result = process_exec.run(allocator, io, args.argv(), 1024, 1024) catch return fallbackSavedActionNotifyIcon(allocator, io, request);
    defer result.deinit(allocator);
    if (result.term == .exited and result.term.exited == 0) {
        return trimActionOutput(allocator, result.stdout);
    }
    return fallbackSavedActionNotifyIcon(allocator, io, request);
}

fn fallbackSavedActionNotifyIcon(
    allocator: std.mem.Allocator,
    io: std.Io,
    request: notify_request.NotificationRequest,
) ![]u8 {
    var args = try notify_request.buildNotifySendArgs(allocator, request, .icon);
    defer args.deinit(allocator);

    const result = process_exec.run(allocator, io, args.argv(), 1024, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);
    if (result.term != .exited or result.term.exited != 0) return error.NotificationUnavailable;
    return trimActionOutput(allocator, result.stdout);
}

fn spawnSavedNotificationActionListener(
    allocator: std.mem.Allocator,
    io: std.Io,
    path: []const u8,
    image_path: ?[]const u8,
) !void {
    const exe = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(exe);

    var argv: [9][]const u8 = undefined;
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = "if [ -n \"$XDG_STATE_HOME\" ]; then dir=\"$XDG_STATE_HOME/shaula\"; elif [ -n \"$HOME\" ]; then dir=\"$HOME/.local/state/shaula\"; else dir=\"/tmp\"; fi; log=\"$dir/notify-actions.log\"; mkdir -p \"$dir\" 2>/dev/null || log=\"/tmp/shaula-notify-actions.log\"; exec \"$@\" >/dev/null 2>>\"$log\" &";
    argv[3] = "shaula-notify-action-listener";
    argv[4] = exe;
    argv[5] = "notify";
    argv[6] = "__saved-action-listener";
    argv[7] = path;
    var argv_len: usize = 8;
    if (image_path) |image| {
        argv[argv_len] = image;
        argv_len += 1;
    }

    const result = process_exec.run(allocator, io, argv[0..argv_len], 1024, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return error.NotificationUnavailable;
}

fn revealViaFileManager1(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const uri = try fileUriAlloc(allocator, io, path);
    defer allocator.free(uri);
    const items = try std.fmt.allocPrint(allocator, "['{s}']", .{uri});
    defer allocator.free(items);

    const result = process_exec.run(allocator, io, &.{
        "gdbus",
        "call",
        "--session",
        "--dest",
        "org.freedesktop.FileManager1",
        "--object-path",
        "/org/freedesktop/FileManager1",
        "--method",
        "org.freedesktop.FileManager1.ShowItems",
        items,
        "",
    }, 4096, 4096) catch return error.RevealFileUnavailable;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return error.RevealFileUnavailable;
}

fn openParentDirectory(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const parent = std.fs.path.dirname(path) orelse "/";
    const result = process_exec.run(allocator, io, &.{ "xdg-open", parent }, 4096, 4096) catch return error.RevealFileUnavailable;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return error.RevealFileUnavailable;
}

fn fileUriAlloc(allocator: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    const absolute = try absolutePathForNotification(allocator, io, path);
    defer allocator.free(absolute);
    return notify_request.fileUriFromPathAlloc(allocator, absolute);
}

fn absolutePathForNotification(allocator: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    if (std.fs.path.isAbsolute(path)) return allocator.dupe(u8, path);
    const result = process_exec.run(allocator, io, &.{ "pwd", "-P" }, 4096, 1024) catch return error.NotificationUnavailable;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return error.NotificationUnavailable;
    const cwd = std.mem.trim(u8, result.stdout, " \t\r\n");
    if (cwd.len == 0) return error.NotificationUnavailable;
    return std.fmt.allocPrint(allocator, "{s}/{s}", .{ cwd, path });
}

fn trimActionOutput(allocator: std.mem.Allocator, stdout: []const u8) ![]u8 {
    return allocator.dupe(u8, std.mem.trim(u8, stdout, " \t\r\n"));
}

fn logRevealFailure(method: []const u8, path: []const u8, err: anyerror) void {
    std.debug.print("shaula reveal-file failed path=\"{s}\" method=\"{s}\" error={s}\n", .{ path, method, @errorName(err) });
}
