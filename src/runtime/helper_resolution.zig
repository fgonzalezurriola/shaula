const std = @import("std");

/// Resolves installed helper binaries from the CLI runtime location.
///
/// Contract constraint: explicit environment overrides win, local/dev installs
/// prefer sibling helper binaries, and PATH fallback keeps package-manager
/// layouts usable when executable-dir discovery is unavailable.
pub fn resolveSiblingHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    env_var: []const u8,
    binary_name: []const u8,
) ![]u8 {
    if (environ.getPosix(env_var)) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, binary_name);
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/{s}", .{ exe_dir, binary_name });
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, binary_name);
    }
}
