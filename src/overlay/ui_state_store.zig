const std = @import("std");
const toolbar_layout = @import("toolbar_layout.zig");
const runtime_paths = @import("../runtime/paths.zig");

/// Persists the last valid capture toolbar position.
///
/// Contract constraints:
/// - the format is a single deterministic `x|y` line.
/// - malformed or missing state resolves to null, never to fabricated UI state.
pub fn store(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    position: toolbar_layout.Point,
) !void {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    try runtime_paths.ensureParent(io, path);

    var file = try std.Io.Dir.createFileAbsolute(io, path, .{ .truncate = true });
    defer file.close(io);

    var buffer: [64]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.print("{d}|{d}\n", .{ position.x, position.y });
    try writer.interface.flush();
}

pub fn load(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?toolbar_layout.Point {
    const path = try resolvePath(allocator, environ);
    defer allocator.free(path);

    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited) catch return null;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (trimmed.len == 0) return null;

    var parts = std.mem.splitScalar(u8, trimmed, '|');
    const x_raw = parts.next() orelse return null;
    const y_raw = parts.next() orelse return null;
    if (parts.next() != null) return null;

    return .{
        .x = std.fmt.parseInt(i32, x_raw, 10) catch return null,
        .y = std.fmt.parseInt(i32, y_raw, 10) catch return null,
    };
}

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    return runtime_paths.resolve(allocator, environ, "SHAULA_TOOLBAR_POSITION_FILE", "overlay/toolbar-position.v1");
}

test "toolbar state path respects explicit override" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_TOOLBAR_POSITION_FILE", "/tmp/shaula/test-toolbar-position.v1");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const path = try resolvePath(std.testing.allocator, .{ .block = block });
    defer std.testing.allocator.free(path);

    try std.testing.expectEqualStrings("/tmp/shaula/test-toolbar-position.v1", path);
}
