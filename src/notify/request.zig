const std = @import("std");

pub const NotifyUrgency = enum {
    low,
    normal,
    critical,

    pub fn asNotifySendArg(self: NotifyUrgency) []const u8 {
        return switch (self) {
            .low => "low",
            .normal => "normal",
            .critical => "critical",
        };
    }
};

pub const NotifyAction = struct {
    id: []const u8,
    label: []const u8,
};

pub const NotificationRequest = struct {
    summary: []const u8,
    body: []const u8,
    image_path: ?[]const u8 = null,
    urgency: NotifyUrgency = .normal,
    timeout_ms: u32 = 2500,
    transient: bool = true,
    action: ?NotifyAction = null,
};

pub const ImageMode = enum {
    hint,
    icon,
};

pub const NotifySendArgs = struct {
    items: [16][]const u8 = undefined,
    len: usize = 0,
    timeout: []u8,
    image_hint: ?[]u8 = null,
    action_arg: ?[]u8 = null,

    pub fn argv(self: *const NotifySendArgs) []const []const u8 {
        return self.items[0..self.len];
    }

    pub fn deinit(self: *NotifySendArgs, allocator: std.mem.Allocator) void {
        allocator.free(self.timeout);
        if (self.image_hint) |hint| allocator.free(hint);
        if (self.action_arg) |action| allocator.free(action);
    }
};

/// Builds notify-send argv for both Freedesktop image-path hints and icon
/// fallback execution.
///
/// Contract constraints:
/// - image hints always use `string:image-path:file://...`.
/// - action arguments use notify-send's `--action=id=label` spelling.
/// - caller owns returned temporary strings through `NotifySendArgs.deinit`.
pub fn buildNotifySendArgs(
    allocator: std.mem.Allocator,
    request: NotificationRequest,
    image_mode: ImageMode,
) !NotifySendArgs {
    var args = NotifySendArgs{
        .timeout = try std.fmt.allocPrint(allocator, "{d}", .{request.timeout_ms}),
    };
    errdefer args.deinit(allocator);

    args.items[args.len] = "notify-send";
    args.len += 1;
    args.items[args.len] = "--app-name=Shaula";
    args.len += 1;
    args.items[args.len] = "--urgency";
    args.len += 1;
    args.items[args.len] = request.urgency.asNotifySendArg();
    args.len += 1;
    args.items[args.len] = "--expire-time";
    args.len += 1;
    args.items[args.len] = args.timeout;
    args.len += 1;

    if (request.transient) {
        args.items[args.len] = "--transient";
        args.len += 1;
    }

    if (request.image_path) |path| {
        switch (image_mode) {
            .hint => {
                const uri = try fileUriFromPathAlloc(allocator, path);
                defer allocator.free(uri);
                args.image_hint = try std.fmt.allocPrint(allocator, "string:image-path:{s}", .{uri});
                args.items[args.len] = "--hint";
                args.len += 1;
                args.items[args.len] = args.image_hint.?;
                args.len += 1;
            },
            .icon => {
                args.items[args.len] = "-i";
                args.len += 1;
                args.items[args.len] = path;
                args.len += 1;
            },
        }
    }

    if (request.action) |action| {
        args.action_arg = try std.fmt.allocPrint(allocator, "--action={s}={s}", .{ action.id, action.label });
        args.items[args.len] = args.action_arg.?;
        args.len += 1;
    }

    args.items[args.len] = request.summary;
    args.len += 1;
    args.items[args.len] = request.body;
    args.len += 1;

    return args;
}

pub fn fileUriFromPathAlloc(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    var out = std.ArrayList(u8).empty;
    errdefer out.deinit(allocator);
    try out.appendSlice(allocator, "file://");
    for (path) |byte| {
        if (byte == '/' or std.ascii.isAlphanumeric(byte) or byte == '-' or byte == '_' or byte == '.' or byte == '~') {
            try out.append(allocator, byte);
        } else {
            try out.print(allocator, "%{X:0>2}", .{byte});
        }
    }
    return out.toOwnedSlice(allocator);
}

test "notify request builds image hint argv" {
    var args = try buildNotifySendArgs(std.testing.allocator, .{
        .summary = "Screenshot captured",
        .body = "Saved.",
        .image_path = "/tmp/shaula/cap one.png",
        .timeout_ms = 6000,
        .action = .{ .id = "default", .label = "Show in folder" },
    }, .hint);
    defer args.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("notify-send", args.argv()[0]);
    try std.testing.expect(containsArg(args.argv(), "--hint"));
    try std.testing.expect(std.mem.indexOf(u8, args.image_hint orelse "", "file:///tmp/shaula/cap%20one.png") != null);
    try std.testing.expect(containsArg(args.argv(), "--action=default=Show in folder"));
}

test "notify request builds icon fallback argv" {
    var args = try buildNotifySendArgs(std.testing.allocator, .{
        .summary = "Screenshot captured",
        .body = "Copied.",
        .image_path = "/tmp/shaula/cap.png",
        .transient = false,
    }, .icon);
    defer args.deinit(std.testing.allocator);

    try std.testing.expect(containsArg(args.argv(), "-i"));
    try std.testing.expect(!containsArg(args.argv(), "--transient"));
}

fn containsArg(argv: []const []const u8, needle: []const u8) bool {
    for (argv) |arg| {
        if (std.mem.eql(u8, arg, needle)) return true;
    }
    return false;
}
