const std = @import("std");
const runtime_paths = @import("paths.zig");

pub const CaptureSessionError = error{
    CaptureInProgress,
};

pub const CaptureSessionLock = struct {
    allocator: std.mem.Allocator,
    io: std.Io,
    path: []u8,
    active: bool = true,

    pub fn release(self: *CaptureSessionLock) void {
        if (!self.active) return;
        std.Io.Dir.deleteFileAbsolute(self.io, self.path) catch {};
        self.active = false;
    }

    pub fn deinit(self: *CaptureSessionLock) void {
        self.release();
        self.allocator.free(self.path);
    }
};

/// Acquires Shaula's cross-process capture gate.
///
/// Contract: the gate covers selection and backend capture only. Callers must
/// release it before post-capture preview work so multiple preview windows can
/// stay open while newer captures start.
pub fn acquire(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !CaptureSessionLock {
    const path = try resolvePath(allocator, environ);
    errdefer allocator.free(path);

    try runtime_paths.ensureParent(io, path);

    var file = std.Io.Dir.createFileAbsolute(io, path, .{ .exclusive = true }) catch |err| switch (err) {
        error.PathAlreadyExists => retry: {
            if (!clearStaleLock(allocator, io, path)) return error.CaptureInProgress;
            break :retry std.Io.Dir.createFileAbsolute(io, path, .{ .exclusive = true }) catch |retry_err| switch (retry_err) {
                error.PathAlreadyExists => return error.CaptureInProgress,
                else => return retry_err,
            };
        },
        else => return err,
    };
    defer file.close(io);

    var buffer: [64]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.print("{d}\n", .{std.os.linux.getpid()});
    try writer.interface.flush();

    return .{
        .allocator = allocator,
        .io = io,
        .path = path,
    };
}

fn clearStaleLock(allocator: std.mem.Allocator, io: std.Io, path: []const u8) bool {
    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(64)) catch return false;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (trimmed.len == 0) return false;

    const pid = std.fmt.parseInt(std.posix.pid_t, trimmed, 10) catch return false;
    std.posix.kill(pid, @enumFromInt(0)) catch |err| switch (err) {
        error.ProcessNotFound => {
            std.Io.Dir.deleteFileAbsolute(io, path) catch return false;
            return true;
        },
        error.PermissionDenied => return false,
        else => return false,
    };

    return false;
}

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    return runtime_paths.resolve(allocator, environ, "SHAULA_CAPTURE_SESSION_LOCK_FILE", "capture/session.lock");
}
