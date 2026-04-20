const std = @import("std");

pub const CaptureMode = enum {
    area,
    fullscreen,
    window,
};

/// Rectangle in compositor layout coordinates.
///
/// This is the canonical geometry payload for area captures and is threaded from
/// overlay/selection into backend execution. Keep units and coordinate space
/// stable because QA and downstream JSON contracts rely on these values.
pub const AreaGeometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

/// Backend input contract for one capture execution.
///
/// Important attributes:
/// - `mode`: capture subcommand semantic target (`area|fullscreen|window`).
/// - `output_path`: optional absolute path override; default path is resolved by backend.
/// - `window_id`: optional explicit target for window mode (currently capability-gated).
/// - `area_geometry`: optional area rectangle; required for real area backend capture.
pub const CaptureRequest = struct {
    mode: CaptureMode,
    output_path: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
    area_geometry: ?AreaGeometry = null,
};

/// Pixel dimensions of the generated image.
///
/// Values are extracted from PNG output and must match persisted/history metadata.
pub const Dimensions = struct {
    width: u32,
    height: u32,
};

/// Success payload returned by capture backend.
///
/// `backend_used` is the resolved backend label. `degraded` indicates an accepted
/// degraded path (e.g. portal fallback semantics) while preserving deterministic
/// command contracts.
pub const CaptureSuccess = struct {
    mode: CaptureMode,
    path: []const u8,
    mime: []const u8,
    dimensions: Dimensions,
    backend_used: []const u8,
    latency_ms: u32,
    degraded: bool,
};

/// Failure payload returned by capture backend.
///
/// `code` must map to taxonomy in `src/errors/taxonomy.zig`. Keep this deterministic
/// because exit-code mapping, QA matrix, and release-readiness gates depend on it.
pub const CaptureFailure = struct {
    mode: CaptureMode,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    degraded: bool,
    backend_used: ?[]const u8,
};

/// Tagged result for one capture backend execution.
pub const CaptureOutcome = union(enum) {
    success: CaptureSuccess,
    failure: CaptureFailure,
};

pub fn modeString(mode: CaptureMode) []const u8 {
    return switch (mode) {
        .area => "area",
        .fullscreen => "fullscreen",
        .window => "window",
    };
}

test "mode string mapping is deterministic" {
    try std.testing.expectEqualStrings("area", modeString(.area));
    try std.testing.expectEqualStrings("fullscreen", modeString(.fullscreen));
    try std.testing.expectEqualStrings("window", modeString(.window));
}
