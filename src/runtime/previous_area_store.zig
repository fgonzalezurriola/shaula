const std = @import("std");
const capture_types = @import("../capture/types.zig");
const runtime_paths = @import("paths.zig");

/// Persists the last committed area geometry for the `previous-area` fast path.
///
/// Contract constraints:
/// - the on-disk format is a single deterministic `x|y|width|height` line.
/// - malformed or missing state resolves to unavailable instead of inventing a
///   geometry, so `capture previous-area` stays honest.
pub fn store(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    geometry: capture_types.AreaGeometry,
) !void {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    try runtime_paths.ensureParent(io, path);

    var file = try std.Io.Dir.createFileAbsolute(io, path, .{ .truncate = true });
    defer file.close(io);

    var buffer: [128]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.print("{d}|{d}|{d}|{d}\n", .{
        geometry.x,
        geometry.y,
        geometry.width,
        geometry.height,
    });
    try writer.interface.flush();
}

pub fn load(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?capture_types.AreaGeometry {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited) catch return null;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (trimmed.len == 0) return null;

    var parts = std.mem.splitScalar(u8, trimmed, '|');
    const x_raw = parts.next() orelse return null;
    const y_raw = parts.next() orelse return null;
    const width_raw = parts.next() orelse return null;
    const height_raw = parts.next() orelse return null;
    if (parts.next() != null) return null;

    const width = std.fmt.parseInt(u32, width_raw, 10) catch return null;
    const height = std.fmt.parseInt(u32, height_raw, 10) catch return null;
    if (width == 0 or height == 0) return null;

    return .{
        .x = std.fmt.parseInt(i32, x_raw, 10) catch return null,
        .y = std.fmt.parseInt(i32, y_raw, 10) catch return null,
        .width = width,
        .height = height,
    };
}

pub fn supportedForBackendLabel(backend_label: []const u8) bool {
    return !std.mem.eql(u8, backend_label, "portal-screenshot");
}

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    return runtime_paths.resolve(allocator, environ, "SHAULA_PREVIOUS_AREA_FILE", "selection/previous-area.v1");
}

test "previous area store path respects explicit override" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_PREVIOUS_AREA_FILE", "/tmp/shaula/test-previous-area.v1");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const path = try resolvePath(std.testing.allocator, .{ .block = block });
    defer std.testing.allocator.free(path);

    try std.testing.expectEqualStrings("/tmp/shaula/test-previous-area.v1", path);
}
