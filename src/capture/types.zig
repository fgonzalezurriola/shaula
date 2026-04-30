const std = @import("std");
const selection = @import("../selection/selection.zig");

pub const CaptureMode = enum {
    area,
    fullscreen,
    focused,
    window,
};

/// Output descriptor used for deterministic local-to-layout normalization.
///
/// Contract constraints:
/// - scale ratio must be strictly positive (`scale_numerator > 0` and
///   `scale_denominator > 0`).
/// - `x`/`y` represent compositor layout origin for that output.
pub const OutputLayout = struct {
    x: i32,
    y: i32,
    scale_numerator: u32 = 1,
    scale_denominator: u32 = 1,
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

/// Converts overlay selection geometry into canonical area geometry.
///
/// This keeps selection→capture conversion centralized so area request
/// construction cannot drift across command/backend boundaries.
pub fn areaGeometryFromSelection(selection_geometry: ?selection.Geometry) ?AreaGeometry {
    const geometry = selection_geometry orelse return null;
    return .{
        .x = geometry.x,
        .y = geometry.y,
        .width = geometry.width,
        .height = geometry.height,
    };
}

/// Normalizes output-local (scaled) geometry into compositor layout coordinates.
///
/// Contract constraints:
/// - deterministic rational rounding is used so fixture assertions are stable in
///   CI/headless runs.
/// - returned dimensions are always non-negative and clipped to `u32`.
pub fn normalizeOutputLocalGeometry(local: AreaGeometry, output: OutputLayout) AreaGeometry {
    if (output.scale_numerator == 0 or output.scale_denominator == 0) {
        return .{
            .x = output.x + local.x,
            .y = output.y + local.y,
            .width = local.width,
            .height = local.height,
        };
    }

    return .{
        .x = output.x + rationalRoundSigned(local.x, output.scale_denominator, output.scale_numerator),
        .y = output.y + rationalRoundSigned(local.y, output.scale_denominator, output.scale_numerator),
        .width = rationalRoundUnsigned(local.width, output.scale_denominator, output.scale_numerator),
        .height = rationalRoundUnsigned(local.height, output.scale_denominator, output.scale_numerator),
    };
}

/// Formats area geometry for capture backend runtime argument (`-g`).
///
/// Returns `null` for absent or zero-sized geometry to preserve deterministic area
/// capture guardrails at runtime boundary formatting.
pub fn formatAreaGeometryArg(area_geometry: ?AreaGeometry, buffer: []u8) ?[]const u8 {
    const geometry = area_geometry orelse return null;
    if (geometry.width == 0 or geometry.height == 0) return null;

    return std.fmt.bufPrint(buffer, "{d},{d} {d}x{d}", .{ geometry.x, geometry.y, geometry.width, geometry.height }) catch null;
}

pub fn modeString(mode: CaptureMode) []const u8 {
    return switch (mode) {
        .area => "area",
        .fullscreen => "fullscreen",
        .focused => "focused",
        .window => "window",
    };
}

fn rationalRoundSigned(value: i32, numerator: u32, denominator: u32) i32 {
    if (value >= 0) {
        const abs_value: i64 = value;
        const rounded = @divFloor(abs_value * @as(i64, @intCast(numerator)) + @divFloor(@as(i64, @intCast(denominator)), 2), @as(i64, @intCast(denominator)));
        return @intCast(rounded);
    }

    const abs_value: i64 = -@as(i64, value);
    const rounded = @divFloor(abs_value * @as(i64, @intCast(numerator)) + @divFloor(@as(i64, @intCast(denominator)), 2), @as(i64, @intCast(denominator)));
    return -@as(i32, @intCast(rounded));
}

fn rationalRoundUnsigned(value: u32, numerator: u32, denominator: u32) u32 {
    const rounded = @divFloor(@as(u64, value) * @as(u64, numerator) + @divFloor(@as(u64, denominator), 2), @as(u64, denominator));
    return @intCast(rounded);
}

test "mode string mapping is deterministic" {
    try std.testing.expectEqualStrings("area", modeString(.area));
    try std.testing.expectEqualStrings("fullscreen", modeString(.fullscreen));
    try std.testing.expectEqualStrings("window", modeString(.window));
}

test "area geometry conversion from selection is deterministic" {
    const converted = areaGeometryFromSelection(.{ .x = 10, .y = -20, .width = 640, .height = 360 }) orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 10), converted.x);
    try std.testing.expectEqual(@as(i32, -20), converted.y);
    try std.testing.expectEqual(@as(u32, 640), converted.width);
    try std.testing.expectEqual(@as(u32, 360), converted.height);
}

test "fractional output local normalization is deterministic" {
    const normalized = normalizeOutputLocalGeometry(
        .{ .x = 100, .y = 60, .width = 250, .height = 125 },
        .{ .x = 1920, .y = 0, .scale_numerator = 4, .scale_denominator = 5 },
    );

    try std.testing.expectEqual(@as(i32, 2045), normalized.x);
    try std.testing.expectEqual(@as(i32, 75), normalized.y);
    try std.testing.expectEqual(@as(u32, 313), normalized.width);
    try std.testing.expectEqual(@as(u32, 156), normalized.height);
}
