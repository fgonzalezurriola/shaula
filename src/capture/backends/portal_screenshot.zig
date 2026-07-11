const std = @import("std");

const c = @cImport({
    @cInclude("runtime/helper_resolution.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn helperSpan(value: []const u8) c.ShaulaRuntimeHelperSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn resolveSiblingHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    env_var: []const u8,
    binary_name: []const u8,
) ![]u8 {
    const executable_dir = std.process.executableDirPathAlloc(io, allocator) catch null;
    defer if (executable_dir) |path| allocator.free(path);

    var owned: c.ShaulaRuntimeHelperOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_helper_owned_path_clear(&owned);
    const status = c.shaula_runtime_helper_resolve(
        envValue(environ, env_var),
        if (executable_dir) |path| helperSpan(path) else .{ .data = null, .length = 0 },
        helperSpan(binary_name),
        &owned,
    );
    return switch (status) {
        c.SHAULA_RUNTIME_HELPER_STATUS_OK => allocator.dupe(u8, owned.data[0..owned.length]),
        c.SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT => error.InvalidPath,
        c.SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        else => error.HelperResolutionFailed,
    };
}

pub const helper_env_var = "SHAULA_PORTAL_SCREENSHOT_HELPER_BIN";
pub const helper_binary = "shaula-portal-screenshot";

/// Resolve the installed portal helper used by the portal backend.
///
/// Contract constraint: the portal backend must use the same helper argv shape as
/// QA runtime helpers, while keeping an explicit override for local diagnostics.
pub fn resolveHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    return resolveSiblingHelper(allocator, io, environ, helper_env_var, helper_binary);
}
