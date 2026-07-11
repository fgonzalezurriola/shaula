const std = @import("std");
const c = @cImport({
    @cInclude("runtime/helper_resolution.h");
    @cInclude("runtime/process_exec.h");
});

fn helperSpan(value: []const u8) c.ShaulaRuntimeHelperSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
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

pub const OverlayRuntimeError = error{
    /// `ERR_OVERLAY_UNAVAILABLE`
    Unavailable,
    /// `ERR_OVERLAY_TIMEOUT`
    Timeout,
    /// `ERR_OVERLAY_PROTOCOL_INVALID`
    ProtocolInvalid,
};

pub const HelperRunResult = struct {
    stdout: []u8,
    stderr: []u8,

    pub fn deinit(self: HelperRunResult, allocator: std.mem.Allocator) void {
        allocator.free(self.stdout);
        allocator.free(self.stderr);
    }
};

/// Runs the overlay helper process through the production stdio seam.
///
/// Contract constraints: helper binary resolution, stdout/stderr limits, and
/// spawn/exit failures map to overlay runtime errors before helper protocol
/// parsing decides the final deterministic `ERR_*` outcome.
pub fn runSelectionHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    helper_env: *std.process.Environ.Map,
) OverlayRuntimeError!HelperRunResult {
    const helper_bin = resolveHelperBinary(allocator, io, environ) catch return error.Unavailable;
    defer allocator.free(helper_bin);

    const argv = [_]c.ShaulaProcessSpan{processSpan(helper_bin)};
    const environment = helper_env.createPosixBlock(allocator, .{}) catch return error.Unavailable;
    defer environment.deinit(allocator);

    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    const status = c.shaula_process_run(
        .{ .items = &argv, .length = argv.len },
        @ptrCast(environment.slice.ptr),
        2048,
        2048,
        &output,
    );
    if (status != c.SHAULA_PROCESS_STATUS_OK) return error.Unavailable;
    if (output.term_kind != c.SHAULA_PROCESS_TERM_EXITED or output.term_value != 0) return error.Unavailable;

    const stdout = allocator.dupe(u8, output.stdout_bytes.data[0..output.stdout_bytes.length]) catch return error.Unavailable;
    errdefer allocator.free(stdout);
    const stderr = allocator.dupe(u8, output.stderr_bytes.data[0..output.stderr_bytes.length]) catch return error.Unavailable;
    return .{ .stdout = stdout, .stderr = stderr };
}

/// Resolves the overlay helper executable used by local builds and installs.
///
/// Contract constraint: local `zig build` output must use the sibling
/// `shaula-overlay` binary before falling back to PATH, otherwise users silently
/// lose the Shaula overlay and only see deterministic overlay unavailability.
pub fn resolveHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    return resolveSiblingHelper(allocator, io, environ, "SHAULA_OVERLAY_HELPER_BIN", "shaula-overlay");
}
