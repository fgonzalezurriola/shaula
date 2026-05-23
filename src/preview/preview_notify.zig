const std = @import("std");
const notify_request = @import("notify_request");

const CInt = c_int;
const TRUE: CInt = 1;
const FALSE: CInt = 0;

extern fn g_spawn_sync(
    working_directory: ?[*:0]const u8,
    argv: [*:null]?[*:0]u8,
    envp: ?[*:null]?[*:0]u8,
    flags: CInt,
    child_setup: ?*const anyopaque,
    user_data: ?*anyopaque,
    standard_output: ?*?[*:0]u8,
    standard_error: ?*?[*:0]u8,
    wait_status: *CInt,
    err: ?*?*anyopaque,
) CInt;

const G_SPAWN_SEARCH_PATH: CInt = 1 << 2;
const G_SPAWN_STDOUT_TO_DEV_NULL: CInt = 1 << 3;
const G_SPAWN_STDERR_TO_DEV_NULL: CInt = 1 << 4;

/// FFI runtime boundary for preview notifications.
///
/// C owns action timing, while Zig owns notify-send argument construction via
/// `notify/request.zig`. Failures are best-effort and reported as FALSE so the
/// preview can log or continue without changing capture `ERR_*` outcomes.
export fn shaula_preview_notify(
    summary_z: ?[*:0]const u8,
    body_z: ?[*:0]const u8,
    image_path_z: ?[*:0]const u8,
    transient_c: CInt,
    timeout_ms_c: CInt,
) CInt {
    const summary = if (summary_z) |value| std.mem.span(value) else return FALSE;
    const body = if (body_z) |value| std.mem.span(value) else return FALSE;
    const image_path = if (image_path_z) |value| imagePathOrNull(value) else null;
    const timeout_ms: u32 = if (timeout_ms_c > 0) @intCast(timeout_ms_c) else 2500;

    const request = notify_request.NotificationRequest{
        .summary = summary,
        .body = body,
        .image_path = image_path,
        .urgency = .normal,
        .timeout_ms = timeout_ms,
        .transient = transient_c != 0,
    };

    if (spawnNotify(request, .hint)) return TRUE;
    if (image_path != null and spawnNotify(request, .icon)) return TRUE;
    return FALSE;
}

fn imagePathOrNull(path_z: [*:0]const u8) ?[]const u8 {
    const path = std.mem.span(path_z);
    return if (path.len == 0) null else path;
}

fn spawnNotify(request: notify_request.NotificationRequest, mode: notify_request.ImageMode) bool {
    const allocator = std.heap.c_allocator;
    var args = notify_request.buildNotifySendArgs(allocator, request, mode) catch return false;
    defer args.deinit(allocator);

    var z_args: [16:null]?[*:0]u8 = [_:null]?[*:0]u8{null} ** 16;
    var owned: [16]?[:0]u8 = [_]?[:0]u8{null} ** 16;
    for (args.argv(), 0..) |arg, i| {
        owned[i] = allocator.dupeZ(u8, arg) catch {
            freeOwned(allocator, &owned);
            return false;
        };
        z_args[i] = owned[i].?.ptr;
    }
    defer freeOwned(allocator, &owned);

    var status: CInt = 1;
    if (g_spawn_sync(
        null,
        &z_args,
        null,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        null,
        null,
        null,
        null,
        &status,
        null,
    ) == FALSE) return false;

    return exitedZero(status);
}

fn freeOwned(allocator: std.mem.Allocator, owned: *[16]?[:0]u8) void {
    for (owned) |maybe_arg| {
        if (maybe_arg) |arg| allocator.free(arg);
    }
}

fn exitedZero(status: CInt) bool {
    return (status & 0x7f) == 0 and ((status >> 8) & 0xff) == 0;
}
