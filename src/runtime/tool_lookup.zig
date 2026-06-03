const std = @import("std");

const env = @import("env.zig");

pub const grim_candidate_paths: []const []const u8 = &.{ "/usr/bin/grim", "/bin/grim", "/usr/local/bin/grim" };

/// Locate runtime tools without changing command execution semantics.
///
/// Contract constraints: capture backends use fixed absolute candidates for
/// deterministic `ERR_CAPTURE_BACKEND_UNAVAILABLE`; diagnostic surfaces may use
/// PATH-aware lookup for user-facing reports.
pub fn findAbsolute(io: std.Io, candidates: []const []const u8) ?[]const u8 {
    for (candidates) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return candidate;
    }
    return null;
}

pub fn grimPath(io: std.Io) ?[]const u8 {
    return findAbsolute(io, grim_candidate_paths);
}

pub fn findInPathAlloc(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    tool: []const u8,
) !?[]u8 {
    const path_env = env.slice(environ, "PATH") orelse return null;
    var parts = std.mem.splitScalar(u8, path_env, ':');
    while (parts.next()) |part| {
        if (part.len == 0) continue;
        const candidate = try std.fmt.allocPrint(allocator, "{s}/{s}", .{ part, tool });
        if (fileExists(io, candidate)) return candidate;
        allocator.free(candidate);
    }
    return null;
}

pub fn fileExists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
}
