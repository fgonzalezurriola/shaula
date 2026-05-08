const std = @import("std");
const execution_plan = @import("capture_execution_plan.zig");
const process_exec = @import("../runtime/process_exec.zig");

/// Dispatch runtime capture to helper or grim and map failures to
/// `error.BackendUnavailable` for deterministic taxonomy handling.
pub fn writeRuntimeCapture(
    io: std.Io,
    environ: std.process.Environ,
    backend_label: []const u8,
    mode_string: []const u8,
    operation: execution_plan.Operation,
    area_geometry: ?[]const u8,
    output_path: []const u8,
) !void {
    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var plan = execution_plan.resolve(std.heap.smp_allocator, io, environ, .{
        .backend_label = backend_label,
        .mode_string = mode_string,
        .operation = operation,
        .area_geometry = area_geometry,
        .output_path = output_path,
    }) catch |err| switch (err) {
        error.BackendUnavailable => return error.BackendUnavailable,
        else => return err,
    };
    defer plan.deinit(std.heap.smp_allocator);

    const result = process_exec.run(
        std.heap.smp_allocator,
        io,
        plan.argv(),
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
