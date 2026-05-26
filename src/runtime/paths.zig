const std = @import("std");

/// Resolves Shaula runtime-state paths through the canonical runtime layout.
///
/// Contract constraints:
/// - explicit env overrides win and are returned exactly after whitespace trim.
/// - non-overridden paths resolve under `$XDG_RUNTIME_DIR/shaula/<relative>`.
/// - when XDG runtime is unavailable, paths fall back to
///   `/tmp/shaula/<relative>`.
pub fn resolve(
    allocator: std.mem.Allocator,
    environ: std.process.Environ,
    env_override_key: ?[]const u8,
    relative_path: []const u8,
) ![]u8 {
    if (env_override_key) |key| {
        if (environ.getPosix(key)) |path_z| {
            const path = std.mem.trim(u8, std.mem.sliceTo(path_z, 0), " \t\r\n");
            if (path.len > 0) return allocator.dupe(u8, path);
        }
    }

    if (environ.getPosix("XDG_RUNTIME_DIR")) |runtime_dir_z| {
        const runtime_dir = std.mem.trim(u8, std.mem.sliceTo(runtime_dir_z, 0), " \t\r\n");
        if (runtime_dir.len > 0) {
            return std.fmt.allocPrint(allocator, "{s}/shaula/{s}", .{ runtime_dir, relative_path });
        }
    }

    return std.fmt.allocPrint(allocator, "/tmp/shaula/{s}", .{relative_path});
}

pub fn ensureParent(io: std.Io, path: []const u8) !void {
    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }
}

pub fn captureArtifactDir(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    return resolve(allocator, environ, null, "captures");
}

/// Detects internal capture staging paths that must not be presented as saved
/// screenshots.
///
/// Contract: this intentionally recognizes the canonical tmp fallback and any
/// runtime-dir path containing `/shaula/captures/`, matching existing preview
/// notification behavior without needing an environment value at call sites.
pub fn isRuntimeCaptureArtifact(path: []const u8) bool {
    if (std.mem.startsWith(u8, path, "/tmp/shaula/captures/")) return true;
    return std.mem.indexOf(u8, path, "/shaula/captures/") != null;
}

test "runtime path respects explicit override" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_TEST_FILE", "/tmp/custom-state.v1");
    try map.put("XDG_RUNTIME_DIR", "/tmp/ignored-runtime");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const path = try resolve(std.testing.allocator, .{ .block = block }, "SHAULA_TEST_FILE", "overlay/state.v1");
    defer std.testing.allocator.free(path);

    try std.testing.expectEqualStrings("/tmp/custom-state.v1", path);
}

test "runtime path uses XDG runtime dir" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("XDG_RUNTIME_DIR", "/tmp/shaula-runtime");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const path = try resolve(std.testing.allocator, .{ .block = block }, null, "overlay/state.v1");
    defer std.testing.allocator.free(path);

    try std.testing.expectEqualStrings("/tmp/shaula-runtime/shaula/overlay/state.v1", path);
}

test "runtime path falls back to tmp" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const path = try resolve(std.testing.allocator, .{ .block = block }, null, "overlay/state.v1");
    defer std.testing.allocator.free(path);

    try std.testing.expectEqualStrings("/tmp/shaula/overlay/state.v1", path);
}

test "runtime capture artifact detection covers canonical prefixes" {
    try std.testing.expect(isRuntimeCaptureArtifact("/tmp/shaula/captures/20260526-091430.png"));
    try std.testing.expect(isRuntimeCaptureArtifact("/run/user/1000/shaula/captures/20260526-091430.png"));
    try std.testing.expect(!isRuntimeCaptureArtifact("/home/me/Pictures/shaula/20260526-091430.png"));
}
