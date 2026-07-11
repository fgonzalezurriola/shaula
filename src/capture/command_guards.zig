const std = @import("std");

const precondition_guard = @import("precondition_guard.zig");
const c = @cImport({
    @cInclude("capabilities/runtime.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};
const command_json = @import("command_json.zig");
const warnings = @import("warnings.zig");

fn capabilitySpan(value: c.ShaulaEnvSpan) []const u8 {
    if (value.length == 0) return "";
    return value.data[0..value.length];
}

fn backendLabel(backend: c.ShaulaBackendKind) []const u8 {
    return capabilitySpan(c.shaula_capabilities_backend_label(backend));
}

fn modeSpan(mode: []const u8) c.ShaulaEnvSpan {
    return .{ .data = mode.ptr, .length = mode.len };
}

fn localRuntime(value: anytype) c.ShaulaRuntimeDecision {
    return .{
        .compositor_supported = value.compositor_supported,
        .overlay_supported = value.overlay_supported,
        .backend = value.backend,
        .capture = .{
            .area = value.capture.area,
            .fullscreen = value.capture.fullscreen,
            .all_screens = value.capture.all_screens,
            .window = value.capture.window,
        },
        .portal_available = value.portal_available,
        .portal_window_capable = value.portal_window_capable,
        .compositor = .{
            .kind = value.compositor.kind,
            .label = .{
                .data = value.compositor.label.data,
                .length = value.compositor.label.length,
            },
        },
    };
}

/// Enforce strict runtime capability contract before backend execution.
///
/// Contract constraints:
/// - Forced stub returns `ERR_CAPTURE_BACKEND_UNAVAILABLE` and uses exit-code mapping.
/// - Unsupported mode returns `ERR_CAPTURE_MODE_UNSUPPORTED` with mismatch marker.
pub fn enforceModeSupported(runtime_arg: anytype, io: std.Io, command: []const u8, mode: []const u8) !?u8 {
    const runtime = localRuntime(runtime_arg);
    if (runtime.backend == c.SHAULA_BACKEND_KIND_STUB) {
        const backend_used = backendLabel(runtime.backend);
        try command_json.writeErrorJson(
            io,
            command,
            "ERR_CAPTURE_BACKEND_UNAVAILABLE",
            "capture backend unavailable",
            true,
            mode,
            backend_used,
            false,
            &.{},
        );

        return recovery_policy.exitCodeFor("ERR_CAPTURE_BACKEND_UNAVAILABLE");
    }

    if (c.shaula_capabilities_mode_supported(runtime.capture, modeSpan(mode)) == 1) {
        return null;
    }

    const backend_used = backendLabel(runtime.backend);
    try command_json.writeErrorJson(
        io,
        command,
        "ERR_CAPTURE_MODE_UNSUPPORTED",
        "capture mode is unsupported by runtime capabilities",
        false,
        mode,
        backend_used,
        modeDegraded(mode),
        &.{warnings.capability_execution_mismatch_guard},
    );

    return recovery_policy.exitCodeFor("ERR_CAPTURE_MODE_UNSUPPORTED");
}

/// Enforce pre-capture shell-artifact guard.
///
/// Returns optional warning token to be surfaced in success/failure JSON.
/// On timeout this function writes deterministic `ERR_CAPTURE_PRECONDITION_TIMEOUT`
/// and returns `error.PreconditionTimeout` for exit-code mapping.
pub fn enforcePreCaptureGuard(
    runtime_arg: anytype,
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    mode: []const u8,
) !?[]const u8 {
    const runtime = localRuntime(runtime_arg);
    const guard_result = try precondition_guard.enforce(allocator, io, environ);
    switch (guard_result) {
        .ok => |ok| return ok.warning,
        .timeout => {
            const backend_used = backendLabel(runtime.backend);

            try command_json.writeErrorJson(
                io,
                command,
                "ERR_CAPTURE_PRECONDITION_TIMEOUT",
                "capture precondition timed out waiting for shell artifact guard",
                true,
                mode,
                backend_used,
                modeDegraded(mode),
                &.{warnings.precondition_guard_timeout},
            );

            return error.PreconditionTimeout;
        },
    }
}

fn modeDegraded(mode: []const u8) bool {
    return std.mem.eql(u8, mode, "window");
}
