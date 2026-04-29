const std = @import("std");
const selection = @import("../selection/selection.zig");
const all_in_one_session = @import("all_in_one_session.zig");
const ui_state_store = @import("ui_state_store.zig");
const previous_area_store = @import("../runtime/previous_area_store.zig");
const selection_draft_store = @import("selection_draft_store.zig");

pub const DraftMode = selection_draft_store.DraftMode;

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
/// deterministic cancellation so caller boundaries emit stable ERR_* outcomes.
pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    draft_mode: DraftMode,
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
        persistSelectionDraft(allocator, io, environ, draft_mode, result) catch {};
        persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
        return result;
    }

    const helper_attempt = try runHelperSelectionAttempt(allocator, io, environ, mode, draft_mode, constraint);
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
/// - helper output mapping must pass through `parseHelperSelectionEnvelope`.
/// - productive runtime never falls back to another selector UI. Helper
///   unavailability maps to deterministic overlay cancellation taxonomy.
fn runHelperSelectionAttempt(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    draft_mode: DraftMode,
    constraint: selection.SelectionConstraint,
) !HelperSelectionAttempt {
    const helper_bin = try resolveHelperBinary(allocator, io, environ);
    defer allocator.free(helper_bin);
    const output_name = try resolveOverlayOutputName(allocator, io, environ);
    defer if (output_name) |name| allocator.free(name);
    const background = try prepareOverlayBackground(allocator, io, environ, output_name);
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

    const helper = std.process.run(allocator, io, .{
        .argv = &.{helper_bin},
        .stdout_limit = .limited(2048),
        .stderr_limit = .limited(2048),
        .environ_map = &helper_env,
    }) catch {
        return .unavailable;
    };
    defer allocator.free(helper.stdout);
    defer allocator.free(helper.stderr);

    if (helperEnvelopeReportsUnavailable(allocator, helper.stdout) catch false) {
        return .unavailable;
    }

    switch (helper.term) {
        .exited => |code| {
            if (code != 0) {
                return .unavailable;
            }
        },
        else => return .unavailable,
    }

    return .{ .selection = parseHelperSelectionEnvelope(allocator, helper.stdout, mode, constraint) };
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

    if (envFlagEnabled(environ, "SHAULA_OVERLAY_DISABLE_BACKGROUND")) {
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

    const result = std.process.run(allocator, io, .{
        .argv = argv,
        .stdout_limit = .limited(1024),
        .stderr_limit = .limited(1024),
    }) catch {
        return null;
    };
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code != 0) {
                std.Io.Dir.deleteFileAbsolute(io, path) catch {};
                allocator.free(path);
                return null;
            }
        },
        else => {
            std.Io.Dir.deleteFileAbsolute(io, path) catch {};
            allocator.free(path);
            return null;
        },
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

    const result = std.process.run(allocator, io, .{
        .argv = &.{ "niri", "msg", "-j", "focused-output" },
        .stdout_limit = .limited(8192),
        .stderr_limit = .limited(1024),
    }) catch return null;
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| if (code != 0) return null,
        else => return null,
    }

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

fn helperEnvelopeReportsUnavailable(allocator: std.mem.Allocator, payload: []const u8) !bool {
    const parsed = std.json.parseFromSlice(OverlayHelperEnvelope, allocator, payload, .{}) catch return false;
    defer parsed.deinit();

    const envelope = parsed.value;
    if (envelope.status != .@"error") return false;

    const helper_error = envelope.@"error" orelse return false;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_UNAVAILABLE")) return true;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_TIMEOUT")) return true;
    return false;
}

/// Resolves the overlay helper executable used by local builds and installs.
///
/// Contract constraint: local `zig build` output must use the sibling
/// `shaula-overlay` binary before falling back to PATH, otherwise users silently
/// lose the Shaula overlay and only see deterministic overlay unavailability.
fn resolveHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-overlay");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-overlay", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-overlay");
    }
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
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_BIN")) |_| {
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
            if (validEnvelopeGeometry(envelope.geometry)) |geometry| {
                return selection.SelectionResult{
                    .mode = mode,
                    .aspect = constraint.aspect,
                    .geometry = .{
                        .x = geometry.x,
                        .y = geometry.y,
                        .width = geometry.width,
                        .height = geometry.height,
                    },
                    .cancelled = true,
                };
            }
            return cancelledSelection(mode, constraint);
        },
    }
}

fn validEnvelopeGeometry(geometry: ?OverlayHelperGeometry) ?OverlayHelperGeometry {
    const value = geometry orelse return null;
    if (value.width == 0 or value.height == 0) return null;
    return value;
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
        if (std.mem.eql(u8, raw_mode, "interaction_move")) break :blk .drag_then_move_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_edge_resize")) break :blk .drag_then_edge_resize_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_nudge")) break :blk .drag_then_nudge_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_large_nudge")) break :blk .drag_then_large_nudge_confirm;
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

test "helper envelope preserves cancelled draft geometry" {
    const result = parseHelperSelectionEnvelope(
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
        .area,
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
    );
    try std.testing.expect(attempt == .unavailable);
}

test "helper runner marks unavailable on configured helper error class" {
    const should_be_unavailable = try helperEnvelopeReportsUnavailable(
        std.testing.allocator,
        "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"helper missing\"}}",
    );
    try std.testing.expect(should_be_unavailable);

    const should_not_be_unavailable = try helperEnvelopeReportsUnavailable(
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
