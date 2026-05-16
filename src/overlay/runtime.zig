const std = @import("std");
const process_exec = @import("../runtime/process_exec.zig");

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

    const helper = process_exec.runWithEnv(allocator, io, &.{helper_bin}, 2048, 2048, helper_env) catch {
        return error.Unavailable;
    };
    defer helper.deinit(allocator);

    if (!helper.exitedZero()) return error.Unavailable;
    return .{
        .stdout = allocator.dupe(u8, helper.stdout) catch return error.Unavailable,
        .stderr = allocator.dupe(u8, helper.stderr) catch return error.Unavailable,
    };
}

/// Resolves the overlay helper executable used by local builds and installs.
///
/// Contract constraint: local `zig build` output must use the sibling
/// `shaula-overlay` binary before falling back to PATH, otherwise users silently
/// lose the Shaula overlay and only see deterministic overlay unavailability.
pub fn resolveHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-overlay");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-overlay", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-overlay");
    }
}
