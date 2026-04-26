const std = @import("std");
const selection = @import("../selection/selection.zig");
const all_in_one_session = @import("all_in_one_session.zig");
const ui_state_store = @import("ui_state_store.zig");

const OverlayHelperStatus = enum {
    ok,
    cancel,
    @"error",
};

const OverlayHelperAction = enum {
    capture,
    cancel,
};

const OverlayHelperGeometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

const OverlayHelperError = struct {
    code: []const u8,
    message: []const u8,
};

const OverlayHelperEnvelope = struct {
    status: OverlayHelperStatus,
    geometry: ?OverlayHelperGeometry = null,
    action: ?OverlayHelperAction = null,
    @"error": ?OverlayHelperError = null,
};

/// Executes overlay selection and maps helper/runtime outputs to SelectionResult.
///
/// Contract constraint: helper contract parsing failures are converted to
/// deterministic cancellation so caller boundaries emit stable ERR_* outcomes.
pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
    is_dry_run: bool,
    simulate_cancel: bool,
) !selection.SelectionResult {
    if (simulate_cancel) {
        return cancelledSelection(mode, constraint);
    }

    if (try deterministicInteractionScenarioPayload(allocator, environ, constraint)) |payload| {
        defer allocator.free(payload);
        return parseHelperSelectionEnvelope(allocator, payload, mode, constraint);
    }

    if (helperTestPayload(environ)) |payload| {
        return parseHelperSelectionEnvelope(allocator, payload, mode, constraint);
    }

    if (is_dry_run) {
        // Return deterministic base area coordinates for testing
        const result = selection.SelectionResult{
            .mode = mode,
            .aspect = constraint.aspect,
            .geometry = .{ .x = 100, .y = 100, .width = 400, .height = 300 },
            .cancelled = false,
        };
        persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
        return result;
    }

    const helper_attempt = try runHelperSelectionAttempt(allocator, io, environ, mode, constraint);
    switch (helper_attempt) {
        .selection => |result| {
            persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
            return result;
        },
        .fallback_to_slurp => {
            const result = runSlurpSelection(allocator, io, mode, constraint);
            persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
            return result;
        },
    }
}

/// Persists only the final valid all-in-one toolbar position after confirmation.
///
/// Contract constraint: cancelled or invalid selections never write UI state, so
/// later sessions do not reuse fabricated positions after deterministic `ERR_*`
/// overlay outcomes.
fn persistToolbarPositionForSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    result: selection.SelectionResult,
) !void {
    if (result.cancelled) return;
    const geometry = result.geometry orelse return;

    const persisted = try ui_state_store.load(allocator, io, environ);
    var session = all_in_one_session.AllInOneSession.init(all_in_one_session.defaultOutput(), persisted);
    session.updateSelection(.{
        .x = geometry.x,
        .y = geometry.y,
        .width = geometry.width,
        .height = geometry.height,
    });
    if (session.toolbar.position) |position| {
        try ui_state_store.store(allocator, io, environ, position);
    }
}

const HelperSelectionAttempt = union(enum) {
    selection: selection.SelectionResult,
    fallback_to_slurp,
};

/// Executes overlay helper first and decides deterministic fallback behavior.
///
/// Contract constraints:
/// - helper output mapping must pass through `parseHelperSelectionEnvelope`.
/// - fallback to `slurp` only occurs for configured helper unavailability/timeout
///   failure classes or helper process/runtime failures.
fn runHelperSelectionAttempt(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
) !HelperSelectionAttempt {
    const helper_bin = helperBinary(environ);

    const helper = std.process.run(allocator, io, .{
        .argv = &.{helper_bin},
        .stdout_limit = .limited(2048),
        .stderr_limit = .limited(2048),
    }) catch {
        return .fallback_to_slurp;
    };
    defer allocator.free(helper.stdout);
    defer allocator.free(helper.stderr);

    if (helperEnvelopeRequiresFallback(allocator, helper.stdout) catch false) {
        return .fallback_to_slurp;
    }

    switch (helper.term) {
        .exited => |code| {
            if (code != 0) {
                return .fallback_to_slurp;
            }
        },
        else => return .fallback_to_slurp,
    }

    return .{ .selection = parseHelperSelectionEnvelope(allocator, helper.stdout, mode, constraint) };
}

fn helperEnvelopeRequiresFallback(allocator: std.mem.Allocator, payload: []const u8) !bool {
    const parsed = std.json.parseFromSlice(OverlayHelperEnvelope, allocator, payload, .{}) catch return false;
    defer parsed.deinit();

    const envelope = parsed.value;
    if (envelope.status != .@"error") return false;

    const helper_error = envelope.@"error" orelse return false;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_UNAVAILABLE")) return true;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_TIMEOUT")) return true;
    return false;
}

fn helperBinary(environ: std.process.Environ) []const u8 {
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_BIN")) |raw_z| {
        return std.mem.sliceTo(raw_z, 0);
    }
    return "shaula-overlay";
}

fn runSlurpSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
) selection.SelectionResult {
    // Functional fallback path while helper runtime is being integrated incrementally.
    const result = std.process.run(allocator, io, .{
        .argv = &.{ "slurp", "-b", "#80808080", "-c", "#FFFFFF" },
        .stdout_limit = .limited(1024),
        .stderr_limit = .limited(1024),
    }) catch {
        return cancelledSelection(mode, constraint);
    };
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code != 0) {
                return cancelledSelection(mode, constraint);
            }
        },
        else => {
            return cancelledSelection(mode, constraint);
        },
    }

    // Parse stdout: "10,20 300x400"
    var x: i32 = 0;
    var y: i32 = 0;
    var w: u32 = 0;
    var h: u32 = 0;

    var it = std.mem.tokenizeAny(u8, result.stdout, " ,x\r\n");
    if (it.next()) |sx| x = std.fmt.parseInt(i32, sx, 10) catch 0;
    if (it.next()) |sy| y = std.fmt.parseInt(i32, sy, 10) catch 0;
    if (it.next()) |sw| w = std.fmt.parseInt(u32, sw, 10) catch 0;
    if (it.next()) |sh| h = std.fmt.parseInt(u32, sh, 10) catch 0;

    return selection.SelectionResult{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = .{ .x = x, .y = y, .width = w, .height = h },
        .cancelled = false,
    };
}

/// Resolve deterministic overlay helper failure taxonomy for cancelled selections.
///
/// Contract constraint: this mapper only emits overlay-specific `ERR_*` codes for
/// helper/runtime boundary failures. Explicit user cancellation must keep mapping
/// to `ERR_SELECTION_CANCELLED` in caller boundaries.
pub fn deterministicFailureCode(
    environ: std.process.Environ,
    simulate_cancel: bool,
    selection_cancelled: bool,
) ?[]const u8 {
    if (!selection_cancelled or simulate_cancel) return null;

    if (environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE")) |raw_mode_z| {
        const raw_mode = std.mem.sliceTo(raw_mode_z, 0);
        if (std.mem.eql(u8, raw_mode, "malformed")) return "ERR_OVERLAY_PROTOCOL_INVALID";
        if (std.mem.eql(u8, raw_mode, "timeout")) return "ERR_OVERLAY_TIMEOUT";
        if (std.mem.eql(u8, raw_mode, "unavailable")) return "ERR_OVERLAY_UNAVAILABLE";
    }

    if (envFlagEnabled(environ, "SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT")) {
        return "ERR_OVERLAY_TIMEOUT";
    }
    if (envFlagEnabled(environ, "SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE")) {
        return "ERR_OVERLAY_UNAVAILABLE";
    }

    return null;
}

/// Parses helper stdio envelope v1 and deterministically maps it to SelectionResult.
///
/// Contract constraints:
/// - `status:"ok"` requires `action:"capture"` and valid non-zero geometry.
/// - `status:"cancel"` and `status:"error"` map to `cancelled=true`.
/// - Malformed payloads are treated as cancelled to preserve deterministic
///   `ERR_SELECTION_CANCELLED` propagation in caller boundaries.
fn parseHelperSelectionEnvelope(
    allocator: std.mem.Allocator,
    payload: []const u8,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
) selection.SelectionResult {
    const parsed = std.json.parseFromSlice(OverlayHelperEnvelope, allocator, payload, .{}) catch {
        return cancelledSelection(mode, constraint);
    };
    defer parsed.deinit();

    const envelope = parsed.value;
    switch (envelope.status) {
        .ok => {
            if (envelope.action == null or envelope.action.? != .capture) {
                return cancelledSelection(mode, constraint);
            }

            const geometry = envelope.geometry orelse return cancelledSelection(mode, constraint);
            if (geometry.width == 0 or geometry.height == 0) {
                return cancelledSelection(mode, constraint);
            }

            return selection.SelectionResult{
                .mode = mode,
                .aspect = constraint.aspect,
                .geometry = .{
                    .x = geometry.x,
                    .y = geometry.y,
                    .width = geometry.width,
                    .height = geometry.height,
                },
                .cancelled = false,
            };
        },
        .cancel, .@"error" => {
            return cancelledSelection(mode, constraint);
        },
    }
}

fn cancelledSelection(mode: selection.SelectionMode, constraint: selection.SelectionConstraint) selection.SelectionResult {
    return selection.SelectionResult{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = null,
        .cancelled = true,
    };
}

fn helperTestPayload(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE")) |raw_mode_z| {
        const raw_mode = std.mem.sliceTo(raw_mode_z, 0);
        if (std.mem.eql(u8, raw_mode, "ok")) {
            return "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":320,\"y\":180,\"width\":640,\"height\":360},\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "cancel")) {
            return "{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "malformed")) {
            return "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":\"bad\",\"y\":1,\"width\":2,\"height\":3},\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "timeout") or std.mem.eql(u8, raw_mode, "unavailable")) {
            return "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_HELPER_TEST\",\"message\":\"forced helper failure\"}}";
        }
    }
    return null;
}

/// Builds deterministic helper-envelope payloads from scripted interaction scenarios.
///
/// Contract constraint: this function only emits stdio envelope v1 payloads so
/// runSelection continues to validate outputs via the parser boundary.
fn deterministicInteractionScenarioPayload(
    allocator: std.mem.Allocator,
    environ: std.process.Environ,
    constraint: selection.SelectionConstraint,
) !?[]u8 {
    const raw_mode_z = environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE") orelse return null;
    const raw_mode = std.mem.sliceTo(raw_mode_z, 0);

    const scenario: selection.DeterministicScenario = blk: {
        if (std.mem.eql(u8, raw_mode, "interaction_drag")) break :blk .drag_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_cancel")) break :blk .drag_cancel_escape;
        if (std.mem.eql(u8, raw_mode, "interaction_resize")) break :blk .drag_then_resize_confirm;
        return null;
    };

    const simulated = selection.simulateDeterministicScenario(.freeform, constraint, scenario);
    if (simulated.cancelled) {
        return try std.fmt.allocPrint(
            allocator,
            "{{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}}",
            .{},
        );
    }

    const geometry = simulated.geometry orelse {
        return try std.fmt.allocPrint(
            allocator,
            "{{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}}",
            .{},
        );
    };

    return try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}},\"error\":null}}",
        .{ geometry.x, geometry.y, geometry.width, geometry.height },
    );
}

fn envFlagEnabled(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
    }
    return false;
}

test "helper envelope maps ok capture to geometry" {
    const result = parseHelperSelectionEnvelope(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":11,\"y\":22,\"width\":333,\"height\":444},\"error\":null}",
        .freeform,
        .{ .aspect = null },
    );

    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 11), geometry.x);
    try std.testing.expectEqual(@as(i32, 22), geometry.y);
    try std.testing.expectEqual(@as(u32, 333), geometry.width);
    try std.testing.expectEqual(@as(u32, 444), geometry.height);
}

test "helper envelope maps cancel status to cancelled result" {
    const result = parseHelperSelectionEnvelope(
        std.testing.allocator,
        "{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}",
        .freeform,
        .{ .aspect = "16:9" },
    );

    try std.testing.expect(result.cancelled);
    try std.testing.expect(result.geometry == null);
    try std.testing.expectEqualStrings("16:9", result.aspect orelse "");
}

test "helper envelope maps malformed payload to cancelled result" {
    const result = parseHelperSelectionEnvelope(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":\"bad\",\"y\":2,\"width\":320,\"height\":200},\"error\":null}",
        .freeform,
        .{ .aspect = null },
    );

    try std.testing.expect(result.cancelled);
    try std.testing.expect(result.geometry == null);
}

test "deterministic failure code maps malformed helper payload to overlay protocol invalid" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE", "malformed");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const code = deterministicFailureCode(.{ .block = block }, false, true);
    try std.testing.expect(code != null);
    try std.testing.expectEqualStrings("ERR_OVERLAY_PROTOCOL_INVALID", code.?);
}

test "deterministic failure code maps forced timeout" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT", "1");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const code = deterministicFailureCode(.{ .block = block }, false, true);
    try std.testing.expect(code != null);
    try std.testing.expectEqualStrings("ERR_OVERLAY_TIMEOUT", code.?);
}

test "deterministic failure code keeps explicit user cancellation mapping" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE", "malformed");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const code = deterministicFailureCode(.{ .block = block }, true, true);
    try std.testing.expect(code == null);
}

test "runSelection maps helper deterministic ok payload before runtime lanes" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_HELPER_BIN", "false");
    try map.put("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE", "ok");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const result = try runSelection(
        std.testing.allocator,
        std.testing.io,
        .{ .block = block },
        .freeform,
        .{ .aspect = null },
        false,
        false,
    );

    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 320), geometry.x);
    try std.testing.expectEqual(@as(i32, 180), geometry.y);
    try std.testing.expectEqual(@as(u32, 640), geometry.width);
    try std.testing.expectEqual(@as(u32, 360), geometry.height);
}

test "helper runner marks fallback when helper process is unavailable" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_OVERLAY_HELPER_BIN", "/definitely/missing/shaula-overlay");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const attempt = try runHelperSelectionAttempt(
        std.testing.allocator,
        std.testing.io,
        .{ .block = block },
        .freeform,
        .{ .aspect = null },
    );
    try std.testing.expect(attempt == .fallback_to_slurp);
}

test "helper runner falls back to slurp on configured helper error class" {
    const should_fallback = try helperEnvelopeRequiresFallback(
        std.testing.allocator,
        "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"helper missing\"}}",
    );
    try std.testing.expect(should_fallback);

    const should_not_fallback = try helperEnvelopeRequiresFallback(
        std.testing.allocator,
        "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_PROTOCOL_INVALID\",\"message\":\"bad payload\"}}",
    );
    try std.testing.expect(!should_not_fallback);
}

test "confirmed selection persists toolbar position" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("SHAULA_TOOLBAR_POSITION_FILE", "/tmp/shaula/test-overlay-toolbar-position.v1");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const result = selection.SelectionResult{
        .mode = .freeform,
        .aspect = null,
        .geometry = .{ .x = 100, .y = 100, .width = 400, .height = 300 },
        .cancelled = false,
    };

    try persistToolbarPositionForSelection(std.testing.allocator, std.testing.io, .{ .block = block }, result);
    const loaded = try ui_state_store.load(std.testing.allocator, std.testing.io, .{ .block = block });
    try std.testing.expect(loaded != null);
}
