const std = @import("std");

pub const CaptureMode = enum {
    area,
    fullscreen,
    window,
};

pub const AreaGeometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

pub const CaptureRequest = struct {
    mode: CaptureMode,
    output_path: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
    area_geometry: ?AreaGeometry = null,
};

pub const Dimensions = struct {
    width: u32,
    height: u32,
};

pub const CaptureSuccess = struct {
    mode: CaptureMode,
    path: []const u8,
    mime: []const u8,
    dimensions: Dimensions,
    backend_used: []const u8,
    latency_ms: u32,
    degraded: bool,
};

pub const CaptureFailure = struct {
    mode: CaptureMode,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    degraded: bool,
    backend_used: ?[]const u8,
};

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
