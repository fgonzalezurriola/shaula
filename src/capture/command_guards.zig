const std = @import("std");

const precondition_guard = @import("precondition_guard.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const recovery_policy = @import("../recovery/policy.zig");
const command_json = @import("command_json.zig");
const warnings = @import("warnings.zig");

/// Enforce strict runtime capability contract before backend execution.
///
/// Contract constraints:
/// - Forced stub returns `ERR_CAPTURE_BACKEND_UNAVAILABLE` and uses exit-code mapping.
/// - Unsupported mode returns `ERR_CAPTURE_MODE_UNSUPPORTED` with mismatch marker.
pub fn enforceModeSupported(runtime: runtime_capabilities.RuntimeDecision, io: std.Io, command: []const u8, mode: []const u8) !?u8 {
    if (runtime.backend == .stub) {
        const backend_used = runtime_capabilities.backendLabel(runtime.backend);
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

    if (runtime_capabilities.modeSupported(runtime.capture, mode)) {
        return null;
    }

    const backend_used = runtime_capabilities.backendLabel(runtime.backend);
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
    runtime: runtime_capabilities.RuntimeDecision,
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    mode: []const u8,
) !?[]const u8 {
    const guard_result = try precondition_guard.enforce(allocator, io, environ);
    switch (guard_result) {
        .ok => |ok| return ok.warning,
        .timeout => {
            const backend_used = runtime_capabilities.backendLabel(runtime.backend);

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
