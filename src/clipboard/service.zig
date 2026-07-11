const std = @import("std");
const c = @cImport({
    @cInclude("runtime/process_exec.h");
});

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

const clipboard_dir = "/tmp/shaula/clipboard";
const clipboard_state_file = "/tmp/shaula/clipboard/current-image.path";

pub const ClipboardCopyResult = struct {
    ok: bool,
    code: ?[]const u8,
    message: ?[]const u8,
};

/// Copy a captured PNG into the persistent clipboard state and Wayland clipboard.
///
/// Contract constraints: unavailable clipboard support returns
/// `ERR_CLIPBOARD_UNAVAILABLE`; publish failures return
/// `ERR_CLIPBOARD_COPY_FAILED` so capture pipelines can degrade explicitly.
pub fn copyImage(io: std.Io, environ: std.process.Environ, path: []const u8) !ClipboardCopyResult {
    if (!isClipboardAvailable(environ)) {
        return .{
            .ok = false,
            .code = "ERR_CLIPBOARD_UNAVAILABLE",
            .message = "clipboard backend is unavailable",
        };
    }

    try std.Io.Dir.cwd().createDirPath(io, clipboard_dir);
    var file = try std.Io.Dir.createFileAbsolute(io, clipboard_state_file, .{ .truncate = true });
    defer file.close(io);

    var writer = file.writer(io, &.{});
    try writer.interface.print("{s}\n", .{path});
    try writer.interface.flush();

    if (!try publishWaylandImage(io, path)) {
        return .{
            .ok = false,
            .code = "ERR_CLIPBOARD_COPY_FAILED",
            .message = "wl-copy could not publish the PNG image to the clipboard",
        };
    }

    return .{ .ok = true, .code = null, .message = null };
}

/// Publishes the captured PNG bytes to the real Wayland clipboard.
///
/// Contract constraint: the state-file copy is kept for Shaula import flows, but
/// this boundary must also write image/png bytes so clipboard managers can see
/// the capture. Failures map to deterministic clipboard `ERR_*` outcomes.
fn publishWaylandImage(io: std.Io, path: []const u8) !bool {
    const bytes = std.Io.Dir.cwd().readFileAlloc(io, path, std.heap.smp_allocator, .unlimited) catch return false;
    defer std.heap.smp_allocator.free(bytes);

    const argv = [_]c.ShaulaProcessSpan{
        processSpan("wl-copy"),
        processSpan("--type"),
        processSpan("image/png"),
    };
    var term_kind: c.ShaulaProcessTermKind = c.SHAULA_PROCESS_TERM_EXITED;
    var term_value: u32 = 0;
    const status = c.shaula_process_run_with_input(
        .{ .items = &argv, .length = argv.len },
        processSpan(bytes),
        &term_kind,
        &term_value,
    );
    if (status != c.SHAULA_PROCESS_STATUS_OK) return false;
    return term_kind == c.SHAULA_PROCESS_TERM_EXITED and term_value == 0;
}

pub fn importImage(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, output_path: ?[]const u8) ![]u8 {
    if (!isClipboardAvailable(environ)) {
        return error.ClipboardUnavailable;
    }

    const source_path = try readCurrentClipboardPath(allocator, io);
    defer allocator.free(source_path);

    const target_path = if (output_path) |explicit|
        try allocator.dupe(u8, explicit)
    else blk: {
        const now = std.Io.Timestamp.now(io, .real).toSeconds();
        break :blk try std.fmt.allocPrint(allocator, "/tmp/shaula/imported-{d}.png", .{now});
    };

    errdefer allocator.free(target_path);
    try copyFileBytesAbsolute(allocator, io, source_path, target_path);
    return target_path;
}

fn isClipboardAvailable(environ: std.process.Environ) bool {
    if (environ.getPosix("SHAULA_CLIPBOARD_AVAILABLE")) |value_z| {
        const value = std.mem.sliceTo(value_z, 0);
        if (std.ascii.eqlIgnoreCase(value, "0") or std.ascii.eqlIgnoreCase(value, "false")) {
            return false;
        }
    }
    return true;
}

fn readCurrentClipboardPath(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    const content = std.Io.Dir.cwd().readFileAlloc(io, clipboard_state_file, allocator, .unlimited) catch {
        return error.ClipboardImportInvalid;
    };
    defer allocator.free(content);

    const trimmed = std.mem.trim(u8, content, "\r\n \t");
    if (trimmed.len == 0) return error.ClipboardImportInvalid;
    return allocator.dupe(u8, trimmed);
}

fn copyFileBytesAbsolute(allocator: std.mem.Allocator, io: std.Io, source_path: []const u8, target_path: []const u8) !void {
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, source_path, allocator, .unlimited);
    defer allocator.free(bytes);

    if (std.fs.path.dirname(target_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var out = try std.Io.Dir.createFileAbsolute(io, target_path, .{ .truncate = true });
    defer out.close(io);
    try out.writeStreamingAll(io, bytes);
}
