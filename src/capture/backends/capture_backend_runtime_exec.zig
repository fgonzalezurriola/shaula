const std = @import("std");
const backend_contract = @import("capture_backend_contract.zig");
const execution_plan = @import("capture_execution_plan.zig");
const c = @cImport({
    @cInclude("runtime/process_exec.h");
});

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn mapProcessStatus(status: c.ShaulaProcessStatus) !void {
    return switch (status) {
        c.SHAULA_PROCESS_STATUS_OK => {},
        c.SHAULA_PROCESS_STATUS_INVALID_ARGUMENT => error.InvalidName,
        c.SHAULA_PROCESS_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        c.SHAULA_PROCESS_STATUS_FILE_NOT_FOUND => error.FileNotFound,
        c.SHAULA_PROCESS_STATUS_ACCESS_DENIED => error.AccessDenied,
        c.SHAULA_PROCESS_STATUS_PERMISSION_DENIED => error.PermissionDenied,
        c.SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE => error.InvalidExe,
        c.SHAULA_PROCESS_STATUS_IS_DIRECTORY => error.IsDir,
        c.SHAULA_PROCESS_STATUS_NOT_DIRECTORY => error.NotDir,
        c.SHAULA_PROCESS_STATUS_FILE_BUSY => error.FileBusy,
        c.SHAULA_PROCESS_STATUS_SYMLINK_LOOP => error.SymLinkLoop,
        c.SHAULA_PROCESS_STATUS_FD_QUOTA => error.SystemFdQuotaExceeded,
        c.SHAULA_PROCESS_STATUS_PROCESS_FD_QUOTA => error.ProcessFdQuotaExceeded,
        c.SHAULA_PROCESS_STATUS_RESOURCE_LIMIT => error.ResourceLimitReached,
        c.SHAULA_PROCESS_STATUS_SYSTEM_RESOURCES => error.SystemResources,
        c.SHAULA_PROCESS_STATUS_NAME_TOO_LONG => error.NameTooLong,
        c.SHAULA_PROCESS_STATUS_FILESYSTEM_ERROR => error.FileSystem,
        c.SHAULA_PROCESS_STATUS_STREAM_TOO_LONG => error.StreamTooLong,
        else => error.Unexpected,
    };
}

/// Dispatch runtime capture to helper, portal, or grim and map failures to
/// deterministic taxonomy-facing errors.
pub fn writeRuntimeCapture(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    backend_label: []const u8,
    mode_string: []const u8,
    operation: execution_plan.Operation,
    area_geometry: ?[]const u8,
    focused_output_name: ?[]const u8,
    output_path: []const u8,
) !void {
    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var plan = execution_plan.resolve(allocator, io, environ, .{
        .backend_label = backend_label,
        .mode_string = mode_string,
        .operation = operation,
        .area_geometry = area_geometry,
        .focused_output_name = focused_output_name,
        .output_path = output_path,
    }) catch |err| switch (err) {
        error.BackendUnavailable => return error.BackendUnavailable,
    };
    defer plan.deinit(allocator);

    const argv = plan.argv();
    const spans = try allocator.alloc(c.ShaulaProcessSpan, argv.len);
    defer allocator.free(spans);
    for (argv, spans) |value, *span| span.* = processSpan(value);

    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    mapProcessStatus(c.shaula_process_run(
        .{ .items = spans.ptr, .length = spans.len },
        null,
        0,
        8192,
        &output,
    )) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };

    if (output.term_kind == c.SHAULA_PROCESS_TERM_EXITED and output.term_value == 0) return;
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
    if (output.term_kind != c.SHAULA_PROCESS_TERM_EXITED) return error.BackendUnavailable;
    return backend_contract.runtimeErrorForExitCode(@intCast(output.term_value));
}
