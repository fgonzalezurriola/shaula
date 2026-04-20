const std = @import("std");

const precondition_guard = @import("precondition_guard.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const recovery_policy = @import("../recovery/policy.zig");
const command_json = @import("command_json.zig");

/// Enforce strict runtime capability contract before backend execution.
///
/// Important behavior:
/// - Forced stub returns `ERR_CAPTURE_BACKEND_UNAVAILABLE`.
/// - Unsupported mode returns `ERR_CAPTURE_MODE_UNSUPPORTED` with mismatch marker.
pub fn enforceModeSupported(io: std.Io, environ: std.process.Environ, command: []const u8, mode: []const u8) !?u8 {
    const runtime = runtime_capabilities.resolve(environ);
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
        &.{"capability_execution_mismatch_guard"},
    );

    return recovery_policy.exitCodeFor("ERR_CAPTURE_MODE_UNSUPPORTED");
}

/// Check if interactive `slurp` binary is available for overlay selection.
pub fn hasInteractiveOverlayBinary(io: std.Io) bool {
    const candidates = [_][]const u8{
        "/usr/bin/slurp",
        "/bin/slurp",
        "/usr/local/bin/slurp",
    };

    for (candidates) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return true;
    }

    return false;
}

/// Enforce pre-capture shell-artifact guard.
///
/// Returns optional warning token to be surfaced in success/failure JSON.
/// On timeout this function writes deterministic error JSON and returns
/// `error.PreconditionTimeout` for exit-code mapping.
pub fn enforcePreCaptureGuard(
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
            const runtime = runtime_capabilities.resolve(environ);
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
                &.{"capture_precondition_guard_timeout"},
            );

            return error.PreconditionTimeout;
        },
    }
}

fn modeDegraded(mode: []const u8) bool {
    return std.mem.eql(u8, mode, "window");
}
