const std = @import("std");
const capture_types = @import("../capture/types.zig");

pub const DraftMode = enum {
    area,
    all_in_one,
};

/// Persists the last visual overlay selection per public overlay mode.
///
/// Contract constraints:
/// - this is UI draft state, not the `previous-area` capture contract.
/// - cancelled overlay sessions may update this state when they include a valid
///   geometry, but malformed state still resolves to null instead of inventing
///   a rectangle.
pub fn store(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: DraftMode,
    geometry: capture_types.AreaGeometry,
) !void {
    const path = try resolvePath(allocator, environ, mode);
    defer allocator.free(path);

    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

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
    mode: DraftMode,
) !?capture_types.AreaGeometry {
    const path = try resolvePath(allocator, environ, mode);
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

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ, mode: DraftMode) ![]u8 {
    const override_key = switch (mode) {
        .area => "SHAULA_OVERLAY_AREA_DRAFT_FILE",
        .all_in_one => "SHAULA_OVERLAY_ALL_IN_ONE_DRAFT_FILE",
    };
    if (environ.getPosix(override_key)) |path_z| {
        const path = std.mem.trim(u8, std.mem.sliceTo(path_z, 0), " \t\r\n");
        if (path.len > 0) return allocator.dupe(u8, path);
    }

    const filename = switch (mode) {
        .area => "area-draft.v1",
        .all_in_one => "all-in-one-draft.v1",
    };

    if (environ.getPosix("XDG_RUNTIME_DIR")) |runtime_dir_z| {
        const runtime_dir = std.mem.trim(u8, std.mem.sliceTo(runtime_dir_z, 0), " \t\r\n");
        if (runtime_dir.len > 0) {
            return std.fmt.allocPrint(allocator, "{s}/shaula/overlay/{s}", .{ runtime_dir, filename });
        }
    }

    return std.fmt.allocPrint(allocator, "/tmp/shaula/overlay/{s}", .{filename});
}

test "selection draft paths are mode-specific" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("XDG_RUNTIME_DIR", "/tmp/shaula-runtime");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const area_path = try resolvePath(std.testing.allocator, .{ .block = block }, .area);
    defer std.testing.allocator.free(area_path);
    const all_path = try resolvePath(std.testing.allocator, .{ .block = block }, .all_in_one);
    defer std.testing.allocator.free(all_path);

    try std.testing.expectEqualStrings("/tmp/shaula-runtime/shaula/overlay/area-draft.v1", area_path);
    try std.testing.expectEqualStrings("/tmp/shaula-runtime/shaula/overlay/all-in-one-draft.v1", all_path);
}
