const std = @import("std");

pub fn writeRuntimeCapture(
    io: std.Io,
    environ: std.process.Environ,
    backend_label: []const u8,
    mode_string: []const u8,
    mode_is_area: bool,
    area_geometry: ?[]const u8,
    output_path: []const u8,
) !void {
    if (configuredRuntimeCaptureHelper(environ)) |helper_path| {
        return writeRuntimeCaptureWithHelper(io, backend_label, mode_string, area_geometry, helper_path, output_path);
    }

    return writeRuntimeCaptureWithGrim(io, mode_is_area, area_geometry, output_path);
}

fn configuredRuntimeCaptureHelper(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_RUNTIME_CAPTURE_HELPER")) |helper_path_z| {
        const configured = std.mem.sliceTo(helper_path_z, 0);
        if (configured.len > 0) return configured;
    }

    return null;
}

fn writeRuntimeCaptureWithHelper(
    io: std.Io,
    backend_label: []const u8,
    mode_string: []const u8,
    area_geometry: ?[]const u8,
    helper_path: []const u8,
    output_path: []const u8,
) !void {
    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    const result = if (area_geometry) |region| std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{
            "python3",
            helper_path,
            "--backend",
            backend_label,
            "--mode",
            mode_string,
            "--geometry",
            region,
            "--output",
            output_path,
        },
        .stdout_limit = .limited(0),
        .stderr_limit = .limited(8192),
    }) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    } else std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{
            "python3",
            helper_path,
            "--backend",
            backend_label,
            "--mode",
            mode_string,
            "--output",
            output_path,
        },
        .stdout_limit = .limited(0),
        .stderr_limit = .limited(8192),
    }) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };
    defer std.heap.smp_allocator.free(result.stdout);
    defer std.heap.smp_allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code == 0) return;
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
        else => {
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
    }
}

fn writeRuntimeCaptureWithGrim(
    io: std.Io,
    mode_is_area: bool,
    area_geometry: ?[]const u8,
    output_path: []const u8,
) !void {
    const grim_path = findGrimBinary(io) orelse return error.BackendUnavailable;

    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    const result = if (mode_is_area) blk: {
        const geometry = area_geometry orelse return error.BackendUnavailable;
        break :blk std.process.run(std.heap.smp_allocator, io, .{
            .argv = &.{ grim_path, "-g", geometry, output_path },
            .stdout_limit = .limited(0),
            .stderr_limit = .limited(8192),
        }) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        };
    } else std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{ grim_path, output_path },
        .stdout_limit = .limited(0),
        .stderr_limit = .limited(8192),
    }) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };
    defer std.heap.smp_allocator.free(result.stdout);
    defer std.heap.smp_allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code == 0) return;
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
        else => {
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
    }
}

fn findGrimBinary(io: std.Io) ?[]const u8 {
    const grim_candidate_paths = [_][]const u8{
        "/usr/bin/grim",
        "/bin/grim",
        "/usr/local/bin/grim",
    };

    for (grim_candidate_paths) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return candidate;
    }
    return null;
}
