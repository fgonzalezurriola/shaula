const std = @import("std");
const c = @cImport({
    @cInclude("notify/request.h");
    @cInclude("runtime/paths.h");
    @cInclude("runtime/process_exec.h");
});

const runtime_paths = struct {
    fn isRuntimeCaptureArtifact(path: []const u8) bool {
        const span: c.ShaulaRuntimePathSpan = .{ .data = path.ptr, .length = path.len };
        return c.shaula_runtime_path_is_capture_artifact(span) != 0;
    }
};

const process_exec = struct {
    const ProcessOutput = struct {
        stdout: []u8,
        stderr: []u8,
        term: std.process.Child.Term,

        fn deinit(self: ProcessOutput, allocator: std.mem.Allocator) void {
            allocator.free(self.stdout);
            allocator.free(self.stderr);
        }

        fn exitedZero(self: ProcessOutput) bool {
            return switch (self.term) {
                .exited => |code| code == 0,
                else => false,
            };
        }
    };

    fn span(value: []const u8) c.ShaulaProcessSpan {
        return .{ .data = value.ptr, .length = value.len };
    }

    fn mapStatus(status: c.ShaulaProcessStatus) !void {
        return switch (status) {
            c.SHAULA_PROCESS_STATUS_OK => {},
            c.SHAULA_PROCESS_STATUS_INVALID_ARGUMENT => error.InvalidName,
            c.SHAULA_PROCESS_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
            c.SHAULA_PROCESS_STATUS_FILE_NOT_FOUND => error.FileNotFound,
            c.SHAULA_PROCESS_STATUS_ACCESS_DENIED => error.AccessDenied,
            c.SHAULA_PROCESS_STATUS_PERMISSION_DENIED => error.PermissionDenied,
            c.SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE => error.InvalidExe,
            c.SHAULA_PROCESS_STATUS_IS_DIRECTORY => error.IsDir,
            c.SHAULA_PROCESS_STATUS_NOT_DIRECTORY => error.NotDir,
            c.SHAULA_PROCESS_STATUS_FILE_BUSY => error.FileBusy,
            c.SHAULA_PROCESS_STATUS_SYMLINK_LOOP => error.SymLinkLoop,
            c.SHAULA_PROCESS_STATUS_FD_QUOTA => error.SystemFdQuotaExceeded,
            c.SHAULA_PROCESS_STATUS_PROCESS_FD_QUOTA => error.ProcessFdQuotaExceeded,
            c.SHAULA_PROCESS_STATUS_RESOURCE_LIMIT => error.ResourceLimitReached,
            c.SHAULA_PROCESS_STATUS_SYSTEM_RESOURCES => error.SystemResources,
            c.SHAULA_PROCESS_STATUS_NAME_TOO_LONG => error.NameTooLong,
            c.SHAULA_PROCESS_STATUS_FILESYSTEM_ERROR => error.FileSystem,
            c.SHAULA_PROCESS_STATUS_STREAM_TOO_LONG => error.StreamTooLong,
            else => error.Unexpected,
        };
    }

    fn term(kind: c.ShaulaProcessTermKind, value: u32) !std.process.Child.Term {
        return switch (kind) {
            c.SHAULA_PROCESS_TERM_EXITED => .{ .exited = @intCast(value) },
            c.SHAULA_PROCESS_TERM_SIGNAL => .{ .signal = @enumFromInt(value) },
            c.SHAULA_PROCESS_TERM_STOPPED => .{ .stopped = @enumFromInt(value) },
            c.SHAULA_PROCESS_TERM_UNKNOWN => .{ .unknown = value },
            else => error.Unexpected,
        };
    }

    fn copyBytes(allocator: std.mem.Allocator, bytes: c.ShaulaProcessOwnedBytes) ![]u8 {
        if (bytes.length == 0) return allocator.alloc(u8, 0);
        return allocator.dupe(u8, bytes.data[0..bytes.length]);
    }

    fn run(
        allocator: std.mem.Allocator,
        io: std.Io,
        argv: []const []const u8,
        stdout_limit: usize,
        stderr_limit: usize,
    ) !ProcessOutput {
        _ = io;
        const spans = try allocator.alloc(c.ShaulaProcessSpan, argv.len);
        defer allocator.free(spans);
        for (argv, spans) |value, *item| item.* = span(value);

        var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
        defer c.shaula_process_output_clear(&output);
        try mapStatus(c.shaula_process_run(
            .{ .items = spans.ptr, .length = spans.len },
            null,
            stdout_limit,
            stderr_limit,
            &output,
        ));

        const stdout = try copyBytes(allocator, output.stdout_bytes);
        errdefer allocator.free(stdout);
        const stderr = try copyBytes(allocator, output.stderr_bytes);
        errdefer allocator.free(stderr);
        return .{ .stdout = stdout, .stderr = stderr, .term = try term(output.term_kind, output.term_value) };
    }
};

const saved_screenshot_notification_timeout_ms: u32 = 6000;
const saved_screenshot_notification_body = "Saved to screenshots folder.";

pub const NotifyUrgency = enum(i32) {
    low = c.SHAULA_NOTIFY_URGENCY_LOW,
    normal = c.SHAULA_NOTIFY_URGENCY_NORMAL,
    critical = c.SHAULA_NOTIFY_URGENCY_CRITICAL,
};

const NotifyAction = struct {
    id: []const u8,
    label: []const u8,
};

fn notifySpan(value: []const u8) c.ShaulaNotifySpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn notificationRequest(
    summary: []const u8,
    body: []const u8,
    image_path: ?[]const u8,
    urgency: NotifyUrgency,
    timeout_ms: u32,
    transient: bool,
    action: ?NotifyAction,
) c.ShaulaNotifyRequest {
    var request: c.ShaulaNotifyRequest = undefined;
    c.shaula_notify_request_init(&request);
    request.summary = notifySpan(summary);
    request.body = notifySpan(body);
    request.urgency = @intFromEnum(urgency);
    request.timeout_ms = timeout_ms;
    request.transient = @intFromBool(transient);
    if (image_path) |path| {
        request.has_image_path = 1;
        request.image_path = notifySpan(path);
    }
    if (action) |value| {
        request.has_action = 1;
        request.action_id = notifySpan(value.id);
        request.action_label = notifySpan(value.label);
    }
    return request;
}

fn mapNotifyStatus(status: c.ShaulaNotifyStatus) !void {
    return switch (status) {
        c.SHAULA_NOTIFY_STATUS_OK => {},
        c.SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        c.SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW => error.Overflow,
        c.SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT,
        c.SHAULA_NOTIFY_STATUS_INVALID_URGENCY,
        c.SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE,
        => error.InvalidNotificationRequest,
        else => error.Unexpected,
    };
}

fn buildNotifySendArgs(
    request: *const c.ShaulaNotifyRequest,
    image_mode: c.ShaulaNotifyImageMode,
) !c.ShaulaNotifySendArgs {
    var args: c.ShaulaNotifySendArgs = std.mem.zeroes(c.ShaulaNotifySendArgs);
    try mapNotifyStatus(c.shaula_notify_send_args_build(request, image_mode, &args));
    return args;
}

fn notifyArgv(
    args: *const c.ShaulaNotifySendArgs,
    storage: *[c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8,
) ![]const []const u8 {
    const length: usize = @intCast(args.length);
    if (length > storage.len) return error.InvalidNotificationRequest;
    for (0..length) |index| {
        const item = args.items[index];
        if (item.data == null and item.length != 0) return error.InvalidNotificationRequest;
        storage[index] = if (item.length == 0) "" else item.data[0..item.length];
    }
    return storage[0..length];
}

fn notifyFileUriAlloc(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    var output: c.ShaulaNotifyOwnedBytes = std.mem.zeroes(c.ShaulaNotifyOwnedBytes);
    defer c.shaula_notify_owned_bytes_clear(&output);
    try mapNotifyStatus(c.shaula_notify_file_uri_build(notifySpan(path), &output));
    if (output.length == 0) return allocator.alloc(u8, 0);
    return allocator.dupe(u8, output.data[0..output.length]);
}

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
    var request = notificationRequest(summary, body, null, urgency, timeout_ms, transient, null);
    var args = try buildNotifySendArgs(&request, c.SHAULA_NOTIFY_IMAGE_MODE_HINT);
    defer c.shaula_notify_send_args_clear(&args);
    var argv_storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &argv_storage);

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch return error.NotificationUnavailable;
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
    var request = notificationRequest(summary, body, image_path, urgency, timeout_ms, transient, null);
    var args = try buildNotifySendArgs(&request, c.SHAULA_NOTIFY_IMAGE_MODE_HINT);
    defer c.shaula_notify_send_args_clear(&args);
    var argv_storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &argv_storage);

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch return fallbackNotifyIcon(allocator, io, request);
    defer result.deinit(allocator);

    if (result.term == .exited and result.term.exited == 0) return;
    try fallbackNotifyIcon(allocator, io, request);
}

fn fallbackNotifyIcon(
    allocator: std.mem.Allocator,
    io: std.Io,
    request: c.ShaulaNotifyRequest,
) !void {
    var request_copy = request;
    var args = try buildNotifySendArgs(&request_copy, c.SHAULA_NOTIFY_IMAGE_MODE_ICON);
    defer c.shaula_notify_send_args_clear(&args);
    var argv_storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &argv_storage);

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch return error.NotificationUnavailable;
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
    var request = notificationRequest(
        summary,
        body,
        image_path,
        urgency,
        timeout_ms,
        transient,
        .{ .id = "default", .label = "Show in folder" },
    );
    var args = try buildNotifySendArgs(&request, c.SHAULA_NOTIFY_IMAGE_MODE_HINT);
    defer c.shaula_notify_send_args_clear(&args);
    var argv_storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &argv_storage);

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch return fallbackSavedActionNotifyIcon(allocator, io, request);
    defer result.deinit(allocator);
    if (result.term == .exited and result.term.exited == 0) {
        return trimActionOutput(allocator, result.stdout);
    }
    return fallbackSavedActionNotifyIcon(allocator, io, request);
}

fn fallbackSavedActionNotifyIcon(
    allocator: std.mem.Allocator,
    io: std.Io,
    request: c.ShaulaNotifyRequest,
) ![]u8 {
    var request_copy = request;
    var args = try buildNotifySendArgs(&request_copy, c.SHAULA_NOTIFY_IMAGE_MODE_ICON);
    defer c.shaula_notify_send_args_clear(&args);
    var argv_storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &argv_storage);

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch return error.NotificationUnavailable;
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
    return notifyFileUriAlloc(allocator, absolute);
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

test "notification request caller uses direct C model" {
    var request = notificationRequest(
        "Screenshot captured",
        "Saved.",
        "/tmp/shaula/cap one.png",
        .normal,
        6000,
        true,
        .{ .id = "default", .label = "Show in folder" },
    );
    var args = try buildNotifySendArgs(&request, c.SHAULA_NOTIFY_IMAGE_MODE_HINT);
    defer c.shaula_notify_send_args_clear(&args);
    var storage: [c.SHAULA_NOTIFY_SEND_ARG_CAPACITY][]const u8 = undefined;
    const argv = try notifyArgv(&args, &storage);

    try std.testing.expectEqual(@as(usize, 12), argv.len);
    try std.testing.expectEqualStrings("notify-send", argv[0]);
    try std.testing.expectEqualStrings("normal", argv[3]);
    try std.testing.expectEqualStrings("6000", argv[5]);
    try std.testing.expectEqualStrings("--transient", argv[6]);
    try std.testing.expectEqualStrings("--hint", argv[7]);
    try std.testing.expectEqualStrings(
        "string:image-path:file:///tmp/shaula/cap%20one.png",
        argv[8],
    );
    try std.testing.expectEqualStrings("--action=default=Show in folder", argv[9]);
    try std.testing.expectEqualStrings("Screenshot captured", argv[10]);
    try std.testing.expectEqualStrings("Saved.", argv[11]);
}
