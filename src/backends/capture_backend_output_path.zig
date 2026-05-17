const std = @import("std");
const runtime_paths = @import("../runtime/paths.zig");

pub fn resolveOutputPath(
    allocator: std.mem.Allocator,
    io: std.Io,
    mode_string: []const u8,
    environ: std.process.Environ,
    output_path: ?[]const u8,
    save_requested: bool,
    save_folder: ?[]const u8,
) ![]u8 {
    if (output_path) |custom_path| {
        if (std.mem.indexOf(u8, custom_path, "::invalid::") != null) {
            return error.OutputPathInvalid;
        }
        return allocator.dupe(u8, custom_path);
    }

    if (!save_requested) {
        return resolveTemporaryOutputPath(allocator, io, mode_string, environ);
    }

    const output_dir = resolveSavedOutputDir(allocator, io, environ, save_folder) catch return error.OutputPathInvalid;
    defer allocator.free(output_dir);

    const ts = std.Io.Timestamp.now(io, .real);
    const millis = ts.toMilliseconds();
    return std.fmt.allocPrint(
        allocator,
        "{s}/capture-{s}-{d}.png",
        .{ output_dir, mode_string, millis },
    );
}

/// Resolve the durable screenshot directory used by explicit save flows.
///
/// Contract: the preferred default is `~/Pictures/shaula`; `~/shaula` is used
/// only when the preferred directory cannot be created or written.
pub fn resolveSavedOutputDir(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    save_folder: ?[]const u8,
) ![]u8 {
    const home = blk: {
        const home_z = environ.getPosix("HOME") orelse return error.OutputPathInvalid;
        const value = std.mem.sliceTo(home_z, 0);
        if (value.len == 0) return error.OutputPathInvalid;
        break :blk value;
    };

    if (save_folder) |folder| {
        if (folder.len > 0) {
            const resolved = try expandSaveFolder(allocator, home, folder);
            errdefer allocator.free(resolved);
            try ensureDirectoryWritable(allocator, io, resolved);
            return resolved;
        }
    }

    const preferred_dir = try std.fmt.allocPrint(allocator, "{s}/Pictures/shaula", .{home});
    defer allocator.free(preferred_dir);
    const fallback_dir = try std.fmt.allocPrint(allocator, "{s}/shaula", .{home});
    defer allocator.free(fallback_dir);

    return chooseOutputDir(allocator, io, preferred_dir, fallback_dir);
}

fn expandSaveFolder(allocator: std.mem.Allocator, home: []const u8, folder: []const u8) ![]u8 {
    if (std.mem.eql(u8, folder, "~")) return allocator.dupe(u8, home);
    if (std.mem.startsWith(u8, folder, "~/")) return std.fmt.allocPrint(allocator, "{s}/{s}", .{ home, folder[2..] });
    if (std.fs.path.isAbsolute(folder)) return allocator.dupe(u8, folder);
    return error.OutputPathInvalid;
}

/// Resolve a non-durable capture artifact path for preview/copy-only flows.
///
/// Contract: implicit captures must not create user-visible saved screenshots;
/// only explicit `--save` or `--output` requests use the durable save directory.
fn resolveTemporaryOutputPath(
    allocator: std.mem.Allocator,
    io: std.Io,
    mode_string: []const u8,
    environ: std.process.Environ,
) ![]u8 {
    const base_dir = try runtime_paths.captureArtifactDir(allocator, environ);
    defer allocator.free(base_dir);

    ensureDirectoryWritable(allocator, io, base_dir) catch return error.OutputPathInvalid;

    const ts = std.Io.Timestamp.now(io, .real);
    const millis = ts.toMilliseconds();
    return std.fmt.allocPrint(
        allocator,
        "{s}/capture-{s}-{d}.png",
        .{ base_dir, mode_string, millis },
    );
}

/// Resolves a writable output directory with a deterministic preference order.
/// Contract: returns an owned absolute path for either `preferred_dir` or
/// `fallback_dir`; when neither is writable it maps to `ERR_OUTPUT_PATH_INVALID`.
fn chooseOutputDir(
    allocator: std.mem.Allocator,
    io: std.Io,
    preferred_dir: []const u8,
    fallback_dir: []const u8,
) ![]u8 {
    ensureDirectoryWritable(allocator, io, preferred_dir) catch {
        try ensureDirectoryWritable(allocator, io, fallback_dir);
        return allocator.dupe(u8, fallback_dir);
    };
    return allocator.dupe(u8, preferred_dir);
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
