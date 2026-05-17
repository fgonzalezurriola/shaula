const std = @import("std");
const capture_types = @import("../capture/types.zig");
const runtime_paths = @import("../runtime/paths.zig");

pub const DraftMode = enum {
    quick,
    area,
    capture,
};

pub const OutputIdentity = struct {
    name: ?[]const u8 = null,
    width: u32,
    height: u32,
    origin_x: i32 = 0,
    origin_y: i32 = 0,
};

const StoredOutputArea = struct {
    output: OutputIdentity,
    geometry: capture_types.AreaGeometry,
};

pub const InitialGeometry = struct {
    geometry: capture_types.AreaGeometry,
    legacy: bool = false,
};

/// Persists the last visual overlay selection per public overlay mode.
///
/// Contract constraints:
/// - this is UI draft state, not the `previous-area` capture contract.
/// - cancelled overlay sessions do not update this state, and malformed state
///   still resolves to null instead of inventing a rectangle.
pub fn store(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: DraftMode,
    geometry: capture_types.AreaGeometry,
) !void {
    const path = try resolvePath(allocator, environ, mode);
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

/// Persist confirmed output-local overlay geometry for the target output.
///
/// Contract constraints:
/// - v2 entries are keyed by output name when available and carry dimensions
///   plus diagnostic origin so cross-monitor restoration never reuses global x/y.
/// - geometry remains output-local; helper JSON emission is the only place that
///   adds the monitor origin for backend capture.
pub fn storeForOutput(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: DraftMode,
    output: OutputIdentity,
    geometry: capture_types.AreaGeometry,
) !void {
    if (output.width == 0 or output.height == 0 or geometry.width == 0 or geometry.height == 0) return;

    const path = try resolvePath(allocator, environ, mode);
    defer allocator.free(path);

    var entries = try loadV2Entries(allocator, io, path);
    defer deinitEntries(allocator, &entries);

    var replaced = false;
    for (entries.items) |*entry| {
        if (sameOutput(entry.output, output)) {
            const owned = try ownedOutputIdentity(allocator, output);
            if (entry.output.name) |name| allocator.free(name);
            entry.* = .{ .output = owned, .geometry = geometry };
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        const owned = try ownedOutputIdentity(allocator, output);
        errdefer if (owned.name) |name| allocator.free(name);
        try entries.append(allocator, .{ .output = owned, .geometry = geometry });
    }

    try runtime_paths.ensureParent(io, path);

    var file = try std.Io.Dir.createFileAbsolute(io, path, .{ .truncate = true });
    defer file.close(io);

    var buffer: [1024]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll("previous_area_version=2\n");
    for (entries.items) |entry| {
        try writer.interface.print("{s}|{d}|{d}|{d}|{d}|{d}|{d}|{d}|{d}\n", .{
            entry.output.name orelse "-",
            entry.output.width,
            entry.output.height,
            entry.output.origin_x,
            entry.output.origin_y,
            entry.geometry.x,
            entry.geometry.y,
            entry.geometry.width,
            entry.geometry.height,
        });
    }
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

pub fn loadForOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: DraftMode,
    output_name: ?[]const u8,
) !?capture_types.AreaGeometry {
    const initial = try loadInitialForOutputName(allocator, io, environ, mode, output_name);
    return if (initial) |value| value.geometry else null;
}

pub fn loadInitialForOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: DraftMode,
    output_name: ?[]const u8,
) !?InitialGeometry {
    const name = output_name orelse return null;
    if (name.len == 0) return null;

    const path = try resolvePath(allocator, environ, mode);
    defer allocator.free(path);

    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited) catch return null;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (trimmed.len == 0) return null;
    if (!std.mem.startsWith(u8, trimmed, "previous_area_version=2")) {
        const legacy = parseLegacyGeometry(trimmed) orelse return null;
        return .{ .geometry = legacy, .legacy = true };
    }

    var lines = std.mem.splitScalar(u8, trimmed, '\n');
    _ = lines.next();
    while (lines.next()) |line_raw| {
        const line = std.mem.trim(u8, line_raw, " \t\r\n");
        if (line.len == 0) continue;
        const entry = parseV2Entry(line) orelse continue;
        if (entry.output.name) |entry_name| {
            if (std.mem.eql(u8, entry_name, name)) return .{ .geometry = entry.geometry };
        }
    }
    return null;
}

fn resolvePath(allocator: std.mem.Allocator, environ: std.process.Environ, mode: DraftMode) ![]u8 {
    const override_key = switch (mode) {
        .quick => "SHAULA_OVERLAY_QUICK_DRAFT_FILE",
        .area => "SHAULA_OVERLAY_AREA_DRAFT_FILE",
        .capture => "SHAULA_OVERLAY_CAPTURE_DRAFT_FILE",
    };
    const primary_override = environ.getPosix(override_key);
    if (primary_override) |path_z| {
        const path = std.mem.trim(u8, std.mem.sliceTo(path_z, 0), " \t\r\n");
        if (path.len > 0) return allocator.dupe(u8, path);
    }
    if (mode == .capture) {
        if (environ.getPosix("SHAULA_OVERLAY_ALL_IN_ONE_DRAFT_FILE")) |path_z| {
            const path = std.mem.trim(u8, std.mem.sliceTo(path_z, 0), " \t\r\n");
            if (path.len > 0) return allocator.dupe(u8, path);
        }
    }

    const filename = switch (mode) {
        .quick => "quick-draft.v1",
        .area => "area-draft.v1",
        .capture => "capture-draft.v1",
    };

    const relative = try std.fmt.allocPrint(allocator, "overlay/{s}", .{filename});
    defer allocator.free(relative);
    return runtime_paths.resolve(allocator, environ, null, relative);
}

fn loadV2Entries(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !std.ArrayListUnmanaged(StoredOutputArea) {
    var entries: std.ArrayListUnmanaged(StoredOutputArea) = .empty;
    errdefer deinitEntries(allocator, &entries);

    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited) catch return entries;
    defer allocator.free(raw);

    const trimmed = std.mem.trim(u8, raw, " \t\r\n");
    if (!std.mem.startsWith(u8, trimmed, "previous_area_version=2")) return entries;

    var lines = std.mem.splitScalar(u8, trimmed, '\n');
    _ = lines.next();
    while (lines.next()) |line_raw| {
        const line = std.mem.trim(u8, line_raw, " \t\r\n");
        if (line.len == 0) continue;
        if (parseV2Entry(line)) |entry| {
            const owned = try ownedOutputIdentity(allocator, entry.output);
            errdefer if (owned.name) |name| allocator.free(name);
            try entries.append(allocator, .{
                .output = owned,
                .geometry = entry.geometry,
            });
        }
    }
    return entries;
}

fn parseV2Entry(line: []const u8) ?StoredOutputArea {
    var parts = std.mem.splitScalar(u8, line, '|');
    const name_raw = parts.next() orelse return null;
    const output_width_raw = parts.next() orelse return null;
    const output_height_raw = parts.next() orelse return null;
    const origin_x_raw = parts.next() orelse return null;
    const origin_y_raw = parts.next() orelse return null;
    const x_raw = parts.next() orelse return null;
    const y_raw = parts.next() orelse return null;
    const width_raw = parts.next() orelse return null;
    const height_raw = parts.next() orelse return null;
    if (parts.next() != null) return null;

    const output_width = std.fmt.parseInt(u32, output_width_raw, 10) catch return null;
    const output_height = std.fmt.parseInt(u32, output_height_raw, 10) catch return null;
    const width = std.fmt.parseInt(u32, width_raw, 10) catch return null;
    const height = std.fmt.parseInt(u32, height_raw, 10) catch return null;
    if (output_width == 0 or output_height == 0 or width == 0 or height == 0) return null;

    return .{
        .output = .{
            .name = if (std.mem.eql(u8, name_raw, "-")) null else name_raw,
            .width = output_width,
            .height = output_height,
            .origin_x = std.fmt.parseInt(i32, origin_x_raw, 10) catch return null,
            .origin_y = std.fmt.parseInt(i32, origin_y_raw, 10) catch return null,
        },
        .geometry = .{
            .x = std.fmt.parseInt(i32, x_raw, 10) catch return null,
            .y = std.fmt.parseInt(i32, y_raw, 10) catch return null,
            .width = width,
            .height = height,
        },
    };
}

fn parseLegacyGeometry(trimmed: []const u8) ?capture_types.AreaGeometry {
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

fn sameOutput(left: OutputIdentity, right: OutputIdentity) bool {
    if (left.name != null and right.name != null) {
        return std.mem.eql(u8, left.name.?, right.name.?);
    }
    return left.name == null and right.name == null and left.width == right.width and left.height == right.height;
}

fn ownedOutputIdentity(allocator: std.mem.Allocator, output: OutputIdentity) !OutputIdentity {
    return .{
        .name = if (output.name) |name| try allocator.dupe(u8, name) else null,
        .width = output.width,
        .height = output.height,
        .origin_x = output.origin_x,
        .origin_y = output.origin_y,
    };
}

fn deinitEntries(allocator: std.mem.Allocator, entries: *std.ArrayListUnmanaged(StoredOutputArea)) void {
    for (entries.items) |entry| {
        if (entry.output.name) |name| allocator.free(name);
    }
    entries.deinit(allocator);
}

test "selection draft paths are mode-specific" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("XDG_RUNTIME_DIR", "/tmp/shaula-runtime");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const area_path = try resolvePath(std.testing.allocator, .{ .block = block }, .area);
    defer std.testing.allocator.free(area_path);
    const capture_path = try resolvePath(std.testing.allocator, .{ .block = block }, .capture);
    defer std.testing.allocator.free(capture_path);

    try std.testing.expectEqualStrings("/tmp/shaula-runtime/shaula/overlay/area-draft.v1", area_path);
    try std.testing.expectEqualStrings("/tmp/shaula-runtime/shaula/overlay/capture-draft.v1", capture_path);
}
