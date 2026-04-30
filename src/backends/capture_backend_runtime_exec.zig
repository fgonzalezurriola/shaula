const std = @import("std");

/// Dispatch runtime capture to helper or grim and map failures to
/// `error.BackendUnavailable` for deterministic taxonomy handling.
pub fn writeRuntimeCapture(
    io: std.Io,
    environ: std.process.Environ,
    backend_label: []const u8,
    mode_string: []const u8,
    mode_is_area: bool,
    mode_is_focused: bool,
    area_geometry: ?[]const u8,
    output_path: []const u8,
) !void {
    if (configuredRuntimeCaptureHelper(environ)) |helper_path| {
        return writeRuntimeCaptureWithHelper(io, backend_label, mode_string, area_geometry, helper_path, output_path);
    }

    return writeRuntimeCaptureWithGrim(io, mode_is_area, mode_is_focused, area_geometry, output_path);
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
    mode_is_focused: bool,
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
    } else if (mode_is_focused) blk: {
        var focused_output_storage: [128]u8 = undefined;
        const focused_output = resolveFocusedOutput(io, &focused_output_storage) orelse return error.BackendUnavailable;
        break :blk std.process.run(std.heap.smp_allocator, io, .{
            .argv = &.{ grim_path, "-o", focused_output, output_path },
            .stdout_limit = .limited(0),
            .stderr_limit = .limited(8192),
        }) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        };
    } else std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{grim_path, output_path},
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

/// Resolve focused output name into the caller buffer.
///
/// Returns null when the focused output cannot be resolved; callers map this
/// to deterministic `ERR_CAPTURE_BACKEND_UNAVAILABLE` handling.
fn resolveFocusedOutput(io: std.Io, buffer: []u8) ?[]const u8 {
    const niri_msg_result = std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{ "niri", "msg", "--json", "focused-output" },
        .stdout_limit = .limited(65536),
        .stderr_limit = .limited(0),
    }) catch return null;
    defer std.heap.smp_allocator.free(niri_msg_result.stdout);
    defer std.heap.smp_allocator.free(niri_msg_result.stderr);

    switch (niri_msg_result.term) {
        .exited => |code| if (code != 0) return null,
        else => return null,
    }

    const stdout = niri_msg_result.stdout;
    const name_key = "\"name\":\"";
    const name_idx = std.mem.indexOfPos(u8, stdout, 0, name_key) orelse return null;
    const name_val_start = name_idx + name_key.len;
    const name_val_end = std.mem.indexOfPos(u8, stdout, name_val_start, "\"") orelse return null;
    const name_len = name_val_end - name_val_start;
    if (name_len == 0 or name_len > buffer.len) return null;

    std.mem.copyForwards(u8, buffer[0..name_len], stdout[name_val_start..name_val_end]);
    return buffer[0..name_len];
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
