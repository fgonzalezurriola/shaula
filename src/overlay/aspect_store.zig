const std = @import("std");

/// Persists the last confirmed interactive Capture Area aspect.
///
/// Contract: this is UI preference state only. Cancelled sessions must not write
/// here, and malformed stored values resolve to null so CLI `--aspect` remains
/// the only hard override for a single invocation.
pub fn store(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    aspect: ?[]const u8,
) !void {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var file = try std.Io.Dir.createFileAbsolute(io, path, .{ .truncate = true });
    defer file.close(io);

    var buffer: [128]u8 = undefined;
    var writer = file.writer(io, &buffer);
    if (aspect) |value| {
        try writer.interface.print("{s}\n", .{value});
    } else {
        try writer.interface.writeAll("Free\n");
    }
    try writer.interface.flush();
}

pub fn load(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?[]u8 {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited) catch return null;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (trimmed.len == 0 or std.ascii.eqlIgnoreCase(trimmed, "Free")) return null;
    if (!validAspect(trimmed)) return null;
    return try allocator.dupe(u8, trimmed);
}

fn validAspect(raw: []const u8) bool {
    var parts = std.mem.splitScalar(u8, raw, ':');
    const w_raw = parts.next() orelse return false;
    const h_raw = parts.next() orelse return false;
    if (parts.next() != null) return false;
    const w = std.fmt.parseInt(u32, w_raw, 10) catch return false;
    const h = std.fmt.parseInt(u32, h_raw, 10) catch return false;
    return w > 0 and h > 0;
}

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_OVERLAY_AREA_ASPECT_FILE")) |path_z| {
        const path = std.mem.trim(u8, std.mem.sliceTo(path_z, 0), " \t\r\n");
        if (path.len > 0) return allocator.dupe(u8, path);
    }

    if (environ.getPosix("XDG_RUNTIME_DIR")) |runtime_dir_z| {
        const runtime_dir = std.mem.trim(u8, std.mem.sliceTo(runtime_dir_z, 0), " \t\r\n");
        if (runtime_dir.len > 0) {
            return std.fmt.allocPrint(allocator, "{s}/shaula/overlay/area-aspect.v1", .{runtime_dir});
        }
    }

    return allocator.dupe(u8, "/tmp/shaula/overlay/area-aspect.v1");
}

test "aspect store roundtrips fixed and free values" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_AREA_ASPECT_FILE", "/tmp/shaula/test-area-aspect-roundtrip.v1");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);
    const environ: std.process.Environ = .{ .block = block };

    try store(std.testing.allocator, std.testing.io, environ, "16:3");
    const fixed = try load(std.testing.allocator, std.testing.io, environ);
    defer if (fixed) |aspect| std.testing.allocator.free(aspect);
    try std.testing.expectEqualStrings("16:3", fixed orelse return error.TestExpectedEqual);

    try store(std.testing.allocator, std.testing.io, environ, null);
    const free = try load(std.testing.allocator, std.testing.io, environ);
    try std.testing.expect(free == null);

    std.Io.Dir.deleteFileAbsolute(std.testing.io, "/tmp/shaula/test-area-aspect-roundtrip.v1") catch {};
}
