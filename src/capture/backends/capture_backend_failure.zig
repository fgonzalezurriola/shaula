const std = @import("std");
/// Build backend failure outcomes from deterministic taxonomy tokens.
///
/// Contract constraint: backend execution must return stable `ERR_*` codes with
/// the same retry/degraded attributes regardless of the internal failure site.
pub fn outcome(
    comptime capture_types: type,
    mode: capture_types.CaptureMode,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    degraded: bool,
    backend_used: ?[]const u8,
) capture_types.CaptureOutcome {
    return .{
        .failure = .{
            .mode = mode,
            .code = code,
            .message = message,
            .retryable = retryable,
            .degraded = degraded,
            .backend_used = backend_used,
        },
    };
}

pub fn unknown(comptime capture_types: type, mode: capture_types.CaptureMode, backend_used: ?[]const u8, message: []const u8) capture_types.CaptureOutcome {
    return outcome(capture_types, mode, "ERR_UNKNOWN_UNMAPPED", message, false, false, backend_used);
}

pub fn backendUnavailable(comptime capture_types: type, mode: capture_types.CaptureMode, backend_used: ?[]const u8) capture_types.CaptureOutcome {
    return outcome(capture_types, mode, "ERR_CAPTURE_BACKEND_UNAVAILABLE", "capture backend unavailable", true, false, backend_used);
}
