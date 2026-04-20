const std = @import("std");

pub fn resolveOutputPath(
    allocator: std.mem.Allocator,
    io: std.Io,
    mode_string: []const u8,
    environ: std.process.Environ,
    output_path: ?[]const u8,
) ![]u8 {
    if (output_path) |custom_path| {
        if (std.mem.indexOf(u8, custom_path, "::invalid::") != null) {
            return error.OutputPathInvalid;
        }
        return allocator.dupe(u8, custom_path);
    }

    const home = blk: {
        const home_z = environ.getPosix("HOME") orelse return error.OutputPathInvalid;
        const value = std.mem.sliceTo(home_z, 0);
        if (value.len == 0) return error.OutputPathInvalid;
        break :blk value;
    };

    const output_dir = try std.fmt.allocPrint(allocator, "{s}/Pictures/Shaula", .{home});
    defer allocator.free(output_dir);
    try ensureDirectoryWritable(allocator, io, output_dir);

    const ts = std.Io.Timestamp.now(io, .real);
    const millis = ts.toMilliseconds();
    return std.fmt.allocPrint(
        allocator,
        "{s}/capture-{s}-{d}.png",
        .{ output_dir, mode_string, millis },
    );
}

fn ensureDirectoryWritable(allocator: std.mem.Allocator, io: std.Io, dir_path: []const u8) !void {
    std.Io.Dir.cwd().createDirPath(io, dir_path) catch return error.OutputPathInvalid;

    const probe_path = try std.fmt.allocPrint(
        allocator,
        "{s}/.shaula-write-probe-{d}.tmp",
        .{ dir_path, std.Io.Timestamp.now(io, .real).toMilliseconds() },
    );
    defer allocator.free(probe_path);

    var probe = std.Io.Dir.createFileAbsolute(io, probe_path, .{ .truncate = true }) catch {
        return error.OutputPathInvalid;
    };
    probe.close(io);
    std.Io.Dir.deleteFileAbsolute(io, probe_path) catch {};
}
