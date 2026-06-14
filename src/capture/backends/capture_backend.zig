const std = @import("std");

const runtime_exec = @import("capture_backend_runtime_exec.zig");
const execution_plan = @import("capture_execution_plan.zig");
const output_path = @import("capture_backend_output_path.zig");
const png_meta = @import("capture_backend_png_meta.zig");
const failure = @import("capture_backend_failure.zig");
const capture_types = @import("../types.zig");
const runtime_capabilities = @import("../../capabilities/runtime.zig");
const env = @import("../../runtime/env.zig");

pub const BackendKind = runtime_capabilities.BackendKind;
pub const RuntimeDecision = runtime_capabilities.RuntimeDecision;

pub const ResolvedExecution = struct {
    runtime: RuntimeDecision,
    focused_output_name: ?[]const u8 = null,
};

/// Execute a single backend capture request and return deterministic outcome.
///
/// This function is a core runtime boundary. It must preserve taxonomy stability
/// (`ERR_*`) and JSON contract expectations used by QA, history persistence, and
/// release-readiness checks. Runtime/backend/focused-output decisions are
/// resolved upstream and passed in so this boundary only executes capture.
pub fn execute(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    resolved: ResolvedExecution,
    request: capture_types.CaptureRequest,
) !capture_types.CaptureOutcome {
    if (environ.getPosix("SHAULA_INJECT_UNKNOWN_FAILURE") != null) {
        return failure.unknown(capture_types, request.mode, null, "injected unknown failure");
    }

    if (!resolved.runtime.compositor_supported) {
        return failure.outcome(capture_types, request.mode, "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, false, null);
    }

    const backend = resolved.runtime.backend;
    const backend_used = resolved.runtime.backendUsedLabel();
    const degraded_backend = resolved.runtime.degradedBackend();

    if (backend == .stub) {
        return failure.backendUnavailable(capture_types, request.mode, backend_used);
    }

    if (request.mode == .window and resolveWindowTarget(request, environ) == null) {
        return failure.outcome(capture_types, .window, "ERR_WINDOW_TARGET_UNRESOLVED", "window target could not be resolved", false, true, backend_used);
    }

    const mode_string = capture_types.modeString(request.mode);
    const resolved_output_path = output_path.resolveOutputPath(
        allocator,
        io,
        mode_string,
        environ,
        request.output_path,
        request.save_requested,
        request.save_folder,
    ) catch |err| switch (err) {
        error.OutputPathInvalid => {
            return failure.outcome(capture_types, request.mode, "ERR_OUTPUT_PATH_INVALID", "output path is not writable", false, false, backend_used);
        },
        else => {
            return failure.unknown(capture_types, request.mode, backend_used, "capture backend failed with unmapped error");
        },
    };
    errdefer allocator.free(resolved_output_path);

    var geometry_storage: [64]u8 = undefined;
    const area_geometry = if (request.mode == .area)
        capture_types.formatAreaGeometryArg(request.area_geometry, &geometry_storage)
    else
        null;

    runtime_exec.writeRuntimeCapture(
        allocator,
        io,
        environ,
        backend_used,
        mode_string,
        operationForMode(request.mode),
        area_geometry,
        resolved.focused_output_name,
        resolved_output_path,
    ) catch |err| switch (err) {
        error.BackendUnavailable => {
            allocator.free(resolved_output_path);
            return failure.backendUnavailable(capture_types, request.mode, backend_used);
        },
        error.IpcTimeout => {
            allocator.free(resolved_output_path);
            return failure.outcome(capture_types, request.mode, "ERR_IPC_TIMEOUT", "IPC operation timed out", true, degraded_backend, backend_used);
        },
        error.SelectionCancelled => {
            allocator.free(resolved_output_path);
            return failure.outcome(capture_types, request.mode, "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, degraded_backend, backend_used);
        },
        error.UnknownUnmapped => {
            allocator.free(resolved_output_path);
            return failure.unknown(capture_types, request.mode, backend_used, "capture backend failed with unmapped error");
        },
        else => {
            allocator.free(resolved_output_path);
            return failure.unknown(capture_types, request.mode, backend_used, "capture backend failed with unmapped error");
        },
    };

    const dimensions_meta = png_meta.resolveCaptureDimensions(allocator, io, resolved_output_path) catch {
        allocator.free(resolved_output_path);
        return failure.backendUnavailable(capture_types, request.mode, backend_used);
    };
    const latency_ms = defaultLatencyMs(request.mode, degraded_backend);

    return .{
        .success = .{
            .mode = request.mode,
            .path = resolved_output_path,
            .mime = "image/png",
            .dimensions = .{ .width = dimensions_meta.width, .height = dimensions_meta.height },
            .backend_used = backend_used,
            .latency_ms = latency_ms,
            .degraded = degraded_backend,
        },
    };
}

pub fn deinitOutcome(allocator: std.mem.Allocator, outcome: *capture_types.CaptureOutcome) void {
    switch (outcome.*) {
        .success => |success| allocator.free(success.path),
        .failure => {},
    }
}

fn operationForMode(mode: capture_types.CaptureMode) execution_plan.Operation {
    return switch (mode) {
        .area => .area,
        .fullscreen, .focused => .current_output,
        .all_screens => .all_outputs,
        .window => .window,
    };
}

fn resolveWindowTarget(request: capture_types.CaptureRequest, environ: std.process.Environ) ?[]const u8 {
    if (request.window_id) |window_id| {
        if (window_id.len > 0) return window_id;
    }

    if (env.trimmed(environ, "SHAULA_WINDOW_ID")) |window_id| {
        return window_id;
    }

    if (env.flagEnabled(environ, "SHAULA_WINDOW_TARGET_RESOLVED")) {
        return "active-window";
    }

    return null;
}

fn defaultLatencyMs(mode: capture_types.CaptureMode, degraded_backend: bool) u32 {
    const base: u32 = switch (mode) {
        .area => 12,
        .fullscreen => 16,
        .all_screens => 16,
        .focused => 14,
        .window => 20,
    };

    if (degraded_backend) return base + 6;
    return base;
}
