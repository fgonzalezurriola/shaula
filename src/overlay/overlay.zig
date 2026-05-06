const std = @import("std");
const selection = @import("../selection/selection.zig");
const all_in_one_session = @import("all_in_one_session.zig");
const helper_protocol = @import("helper_protocol.zig");
const ui_state_store = @import("ui_state_store.zig");
const previous_area_store = @import("../runtime/previous_area_store.zig");
const process_exec = @import("../runtime/process_exec.zig");
const selection_draft_store = @import("selection_draft_store.zig");
const overlay_runtime = @import("runtime.zig");

pub const DraftMode = selection_draft_store.DraftMode;
pub const RegionCaptureMode = @import("../core/capture_mode.zig").RegionCaptureMode;
pub const deterministicFailureCode = helper_protocol.deterministicFailureCode;

const NiriFocusedOutput = struct {
    name: []const u8,
};

const OverlayBackground = struct {
    path: []u8,
    cleanup: bool,

    fn deinit(self: OverlayBackground, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.cleanup) {
            std.Io.Dir.deleteFileAbsolute(io, self.path) catch {};
        }
        allocator.free(self.path);
    }
};

/// Executes overlay selection and maps helper/runtime outputs to SelectionResult.
///
/// Contract constraint: helper contract parsing failures are converted to
/// deterministic cancellation so caller boundaries emit stable `ERR_*` outcomes.
pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    draft_mode: DraftMode,
    constraint: selection.SelectionConstraint,
    region_capture_mode: RegionCaptureMode,
    is_dry_run: bool,
    simulate_cancel: bool,
) !selection.SelectionResult {
    if (simulate_cancel) {
        return cancelledSelection(mode, constraint);
    }

    if (try helper_protocol.deterministicInteractionScenarioPayload(allocator, environ, constraint)) |payload| {
        defer allocator.free(payload);
        return helper_protocol.parseSelectionEnvelope(allocator, payload, mode, constraint);
    }

    if (helper_protocol.testPayload(environ)) |payload| {
        return helper_protocol.parseSelectionEnvelope(allocator, payload, mode, constraint);
    }

    if (is_dry_run) {
        // Return deterministic base area coordinates for testing
        const result = selection.SelectionResult{
            .mode = mode,
            .aspect = constraint.aspect,
            .geometry = .{ .x = 100, .y = 100, .width = 400, .height = 300 },
            .cancelled = false,
        };
        persistSelectionDraft(allocator, io, environ, draft_mode, result) catch {};
        persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
        return result;
    }

    const helper_attempt = try runHelperSelectionAttempt(allocator, io, environ, mode, draft_mode, constraint, region_capture_mode);
    switch (helper_attempt) {
        .selection => |result| {
            persistSelectionDraft(allocator, io, environ, draft_mode, result) catch {};
            persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
            return result;
        },
        .unavailable => return cancelledSelection(mode, constraint),
    }
}

fn persistSelectionDraft(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    draft_mode: DraftMode,
    result: selection.SelectionResult,
) !void {
    const geometry = captureAreaGeometryFromSelection(result.geometry) orelse return;
    try selection_draft_store.store(allocator, io, environ, draft_mode, geometry);
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
    unavailable,
};

/// Executes overlay helper first and decides deterministic fallback behavior.
///
/// Contract constraints:
/// - helper output mapping must pass through `helper_protocol.parseSelectionEnvelope`.
/// - productive runtime never falls back to another selector UI. Helper
///   unavailability maps to deterministic overlay cancellation taxonomy.
fn runHelperSelectionAttempt(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    draft_mode: DraftMode,
    constraint: selection.SelectionConstraint,
    region_capture_mode: RegionCaptureMode,
) !HelperSelectionAttempt {
    const output_name = try resolveOverlayOutputName(allocator, io, environ);
    defer if (output_name) |name| allocator.free(name);
    const background = if (region_capture_mode == .frozen)
        try prepareOverlayBackground(allocator, io, environ, output_name)
    else
        null;
    defer if (background) |prepared| prepared.deinit(allocator, io);

    var helper_env = try std.process.Environ.createMap(environ, allocator);
    defer helper_env.deinit();
    if (constraint.aspect) |aspect| {
        try helper_env.put("SHAULA_OVERLAY_ASPECT", aspect);
    }
    if (background) |prepared| {
        try helper_env.put("SHAULA_OVERLAY_BACKGROUND_PATH", prepared.path);
    }
    if (output_name) |name| {
        try helper_env.put("SHAULA_OVERLAY_OUTPUT_NAME", name);
    }
    const initial = (try selection_draft_store.load(allocator, io, environ, draft_mode)) orelse
        (try previous_area_store.load(allocator, io, environ));
    if (initial) |geometry| {
        const initial_geometry = try std.fmt.allocPrint(allocator, "{d},{d},{d},{d}", .{
            geometry.x,
            geometry.y,
            geometry.width,
            geometry.height,
        });
        defer allocator.free(initial_geometry);
        try helper_env.put("SHAULA_OVERLAY_INITIAL_GEOMETRY", initial_geometry);
    }

    const helper = overlay_runtime.runSelectionHelper(allocator, io, environ, &helper_env) catch {
        return .unavailable;
    };
    defer helper.deinit(allocator);

    if (helper_protocol.reportsUnavailable(allocator, helper.stdout) catch false) {
        return .unavailable;
    }

    return .{ .selection = helper_protocol.parseSelectionEnvelope(allocator, helper.stdout, mode, constraint) };
}

/// Prepares the optional frozen-screen visual background for the helper.
///
/// Contract constraint: this is a visual-only best-effort path. Capture failure
/// here must not become an overlay `ERR_*` outcome or fabricate a selection.
fn prepareOverlayBackground(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    output_name: ?[]const u8,
) !?OverlayBackground {
    if (environ.getPosix("SHAULA_OVERLAY_BACKGROUND_PATH")) |existing_z| {
        const existing = std.mem.trim(u8, std.mem.sliceTo(existing_z, 0), " \t\r\n");
        if (existing.len > 0) {
            return .{ .path = try allocator.dupe(u8, existing), .cleanup = false };
        }
    }

    if (helper_protocol.envFlagEnabled(environ, "SHAULA_OVERLAY_DISABLE_BACKGROUND")) {
        return null;
    }

    const dir = try overlayRuntimeDir(allocator, environ);
    defer allocator.free(dir);
    std.Io.Dir.cwd().createDirPath(io, dir) catch return null;

    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    const safe_millis: i64 = if (millis < 0) 0 else millis;
    const path = try std.fmt.allocPrint(allocator, "{s}/background-{d}.png", .{ dir, safe_millis });
    errdefer allocator.free(path);

    const argv: []const []const u8 = if (output_name) |name|
        &.{ "grim", "-o", name, path }
    else
        &.{ "grim", path };

    const result = process_exec.run(allocator, io, argv, 1024, 1024) catch {
        return null;
    };
    defer result.deinit(allocator);

    if (!result.exitedZero()) {
        std.Io.Dir.deleteFileAbsolute(io, path) catch {};
        allocator.free(path);
        return null;
    }

    return .{ .path = path, .cleanup = true };
}

/// Resolves the focused Niri output for monitor-scoped overlay and preview capture.
///
/// Contract constraint: output resolution is advisory. Failure falls back to
/// compositor-chosen placement and full-output screenshot behavior.
fn resolveOverlayOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?[]u8 {
    if (environ.getPosix("SHAULA_OVERLAY_OUTPUT_NAME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return try allocator.dupe(u8, raw);
    }

    if (environ.getPosix("NIRI_SOCKET") == null) return null;

    const result = process_exec.run(allocator, io, &.{ "niri", "msg", "-j", "focused-output" }, 8192, 1024) catch return null;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return null;

    const parsed = std.json.parseFromSlice(NiriFocusedOutput, allocator, result.stdout, .{
        .ignore_unknown_fields = true,
    }) catch return null;
    defer parsed.deinit();
    if (parsed.value.name.len == 0) return null;

    return try allocator.dupe(u8, parsed.value.name);
}

fn overlayRuntimeDir(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("XDG_RUNTIME_DIR")) |runtime_dir_z| {
        const runtime_dir = std.mem.trim(u8, std.mem.sliceTo(runtime_dir_z, 0), " \t\r\n");
        if (runtime_dir.len > 0) {
            return std.fmt.allocPrint(allocator, "{s}/shaula/overlay", .{runtime_dir});
        }
    }
    return allocator.dupe(u8, "/tmp/shaula/overlay");
}

fn captureAreaGeometryFromSelection(geometry: ?selection.Geometry) ?@import("../capture/types.zig").AreaGeometry {
    const value = geometry orelse return null;
    if (value.width == 0 or value.height == 0) return null;
    return .{
        .x = value.x,
        .y = value.y,
        .width = value.width,
        .height = value.height,
    };
}

fn cancelledSelection(mode: selection.SelectionMode, constraint: selection.SelectionConstraint) selection.SelectionResult {
    return selection.SelectionResult{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = null,
        .cancelled = true,
    };
}

test "helper envelope maps ok capture to geometry" {
    const result = helper_protocol.parseSelectionEnvelope(
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
    const result = helper_protocol.parseSelectionEnvelope(
        std.testing.allocator,
        "{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}",
        .freeform,
        .{ .aspect = "16:9" },
    );

    try std.testing.expect(result.cancelled);
    try std.testing.expect(result.geometry == null);
    try std.testing.expectEqualStrings("16:9", result.aspect orelse "");
}

test "helper envelope preserves cancelled draft geometry" {
    const result = helper_protocol.parseSelectionEnvelope(
        std.testing.allocator,
        "{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":{\"x\":40,\"y\":50,\"width\":600,\"height\":400},\"error\":null}",
        .freeform,
        .{ .aspect = null },
    );

    try std.testing.expect(result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 40), geometry.x);
    try std.testing.expectEqual(@as(i32, 50), geometry.y);
    try std.testing.expectEqual(@as(u32, 600), geometry.width);
    try std.testing.expectEqual(@as(u32, 400), geometry.height);
}

test "helper envelope maps malformed payload to cancelled result" {
    const result = helper_protocol.parseSelectionEnvelope(
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
        .area,
        .{ .aspect = null },
        .live,
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

test "helper runner marks unavailable when helper process is unavailable" {
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
        .area,
        .{ .aspect = null },
        .live,
    );
    try std.testing.expect(attempt == .unavailable);
}

test "helper runner marks unavailable on configured helper error class" {
    const should_be_unavailable = try helper_protocol.reportsUnavailable(
        std.testing.allocator,
        "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"helper missing\"}}",
    );
    try std.testing.expect(should_be_unavailable);

    const should_not_be_unavailable = try helper_protocol.reportsUnavailable(
        std.testing.allocator,
        "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_PROTOCOL_INVALID\",\"message\":\"bad payload\"}}",
    );
    try std.testing.expect(!should_not_be_unavailable);
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
