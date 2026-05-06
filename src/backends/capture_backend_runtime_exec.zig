const std = @import("std");
const process_exec = @import("../runtime/process_exec.zig");

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

    const result = if (area_geometry) |region| process_exec.run(std.heap.smp_allocator, io, &.{
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
        0,
        8192,
    ) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    } else process_exec.run(std.heap.smp_allocator, io, &.{
            helper_path,
            "--backend",
            backend_label,
            "--mode",
            mode_string,
            "--output",
            output_path,
        },
        0,
        8192,
    ) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };
    defer result.deinit(std.heap.smp_allocator);

    if (result.exitedZero()) {
        return;
    }
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
    return error.BackendUnavailable;
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
        break :blk process_exec.run(std.heap.smp_allocator, io, &.{ grim_path, "-g", geometry, output_path }, 0, 8192) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        };
    } else if (mode_is_focused) blk: {
        var focused_output_storage: [128]u8 = undefined;
        const focused_output = resolveFocusedOutput(io, &focused_output_storage) orelse return error.BackendUnavailable;
        break :blk process_exec.run(std.heap.smp_allocator, io, &.{ grim_path, "-o", focused_output, output_path }, 0, 8192) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        };
    } else process_exec.run(std.heap.smp_allocator, io, &.{ grim_path, output_path }, 0, 8192) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };
    defer result.deinit(std.heap.smp_allocator);

    if (result.exitedZero()) {
        return;
    }
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
    return error.BackendUnavailable;
}

/// Resolve focused output name into the caller buffer.
///
/// Returns null when the focused output cannot be resolved; callers map this
/// to deterministic `ERR_CAPTURE_BACKEND_UNAVAILABLE` handling.
fn resolveFocusedOutput(io: std.Io, buffer: []u8) ?[]const u8 {
    const niri_msg_result = process_exec.run(std.heap.smp_allocator, io, &.{ "niri", "msg", "--json", "focused-output" }, 65536, 0) catch return null;
    defer niri_msg_result.deinit(std.heap.smp_allocator);
    if (!niri_msg_result.exitedZero()) return null;

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
