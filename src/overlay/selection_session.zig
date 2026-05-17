const std = @import("std");
const selection = @import("../selection/selection.zig");
const capture_session = @import("capture_session.zig");
const helper_protocol = @import("helper_protocol.zig");
const ui_state_store = @import("ui_state_store.zig");
const aspect_store = @import("aspect_store.zig");
const process_exec = @import("../runtime/process_exec.zig");
const runtime_paths = @import("../runtime/paths.zig");
const selection_draft_store = @import("selection_draft_store.zig");
const overlay_runtime = @import("runtime.zig");
const compositor_focused_output = @import("../compositor/focused_output.zig");

pub const DraftMode = selection_draft_store.DraftMode;
pub const RegionCaptureMode = @import("../core/capture_mode.zig").RegionCaptureMode;
pub const deterministicFailureCode = helper_protocol.deterministicFailureCode;

pub const InteractionMode = enum {
    quick,
    area,

    fn asEnvValue(mode: InteractionMode) []const u8 {
        return switch (mode) {
            .quick => "quick",
            .area => "area",
        };
    }
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

/// Executes a complete overlay selection session.
///
/// Contract constraints:
/// - helper contract parsing failures are converted to deterministic
///   cancellation so caller boundaries emit stable `ERR_*` outcomes.
/// - accepted selections are the only path that persists draft and toolbar UI
///   state; runtime preparation and protocol parsing stay internal.
pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    interaction_mode: InteractionMode,
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

    const helper_attempt = try runHelperSelectionAttempt(allocator, io, environ, mode, interaction_mode, draft_mode, constraint, region_capture_mode);
    switch (helper_attempt) {
        .selection => |helper_selection_raw| {
            const helper_selection = helper_selection_raw;
            defer helper_selection.deinit(allocator);
            const result = helper_selection.result;
            persistSelectionDraft(allocator, io, environ, draft_mode, result) catch {};
            persistOutputAwareSelectionDraft(allocator, io, environ, draft_mode, helper_selection) catch {};
            persistSelectionAspect(allocator, io, environ, interaction_mode, result, helper_selection) catch {};
            persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
            return result;
        },
        .unavailable => return cancelledSelection(mode, constraint),
    }
}

fn persistSelectionAspect(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    interaction_mode: InteractionMode,
    result: selection.SelectionResult,
    helper_selection: HelperSelection,
) !void {
    if (interaction_mode != .area or result.cancelled) return;
    if (helper_selection.final_aspect_known) {
        try aspect_store.store(allocator, io, environ, helper_selection.final_aspect);
        return;
    }
    try aspect_store.store(allocator, io, environ, result.aspect);
}

fn persistSelectionDraft(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    draft_mode: DraftMode,
    result: selection.SelectionResult,
) !void {
    if (result.cancelled) return;
    const geometry = captureAreaGeometryFromSelection(result.geometry) orelse return;
    try selection_draft_store.store(allocator, io, environ, draft_mode, geometry);
}

fn persistOutputAwareSelectionDraft(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    draft_mode: DraftMode,
    helper_selection: HelperSelection,
) !void {
    if (helper_selection.result.cancelled) return;
    const local = helper_selection.local_selection orelse return;
    try selection_draft_store.storeForOutput(allocator, io, environ, draft_mode, .{
        .name = local.output_name,
        .width = local.output_width,
        .height = local.output_height,
        .origin_x = local.output_origin_x,
        .origin_y = local.output_origin_y,
    }, local.geometry);
}

/// Persists only the final valid capture toolbar position after confirmation.
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
    var session = capture_session.CaptureSession.init(capture_session.defaultOutput(), persisted);
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
    selection: HelperSelection,
    unavailable,
};

const HelperSelection = struct {
    result: selection.SelectionResult,
    final_aspect_known: bool = false,
    final_aspect: ?[]u8 = null,
    local_selection: ?helper_protocol.LocalSelection = null,
    debug_stderr: ?[]const u8 = null,

    fn deinit(self: HelperSelection, allocator: std.mem.Allocator) void {
        if (self.final_aspect) |aspect| allocator.free(aspect);
        if (self.local_selection) |local| local.deinit(allocator);
        if (self.debug_stderr) |stderr| allocator.free(stderr);
    }
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
    interaction_mode: InteractionMode,
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
    try helper_env.put("SHAULA_OVERLAY_INTERACTION_MODE", interaction_mode.asEnvValue());

    const stored_area_aspect = if (interaction_mode == .area and constraint.aspect == null)
        try aspect_store.load(allocator, io, environ)
    else
        null;
    defer if (stored_area_aspect) |aspect| allocator.free(aspect);

    const helper_aspect = constraint.aspect orelse stored_area_aspect;
    if (helper_aspect) |aspect| {
        try helper_env.put("SHAULA_OVERLAY_ASPECT", aspect);
    }
    if (background) |prepared| {
        try helper_env.put("SHAULA_OVERLAY_BACKGROUND_PATH", prepared.path);
    }
    if (output_name) |name| {
        try helper_env.put("SHAULA_OVERLAY_OUTPUT_NAME", name);
    }
    const initial = try selection_draft_store.loadInitialForOutputName(allocator, io, environ, draft_mode, output_name);
    if (initial) |geometry| {
        const initial_geometry = try std.fmt.allocPrint(allocator, "{d},{d},{d},{d}", .{
            geometry.geometry.x,
            geometry.geometry.y,
            geometry.geometry.width,
            geometry.geometry.height,
        });
        defer allocator.free(initial_geometry);
        try helper_env.put("SHAULA_OVERLAY_INITIAL_GEOMETRY", initial_geometry);
        if (geometry.legacy) {
            try helper_env.put("SHAULA_OVERLAY_INITIAL_GEOMETRY_LEGACY", "1");
        }
    }

    const debug_latency = helper_protocol.envFlagEnabled(environ, "SHAULA_DEBUG_OVERLAY_LATENCY");
    if (debug_latency) {
        try helper_env.put("SHAULA_DEBUG_OVERLAY_LATENCY", "1");
    }
    const launch_ts: i64 = if (debug_latency) std.Io.Timestamp.now(io, .real).toMilliseconds() else 0;

    const helper = overlay_runtime.runSelectionHelper(allocator, io, environ, &helper_env) catch {
        return .unavailable;
    };
    defer helper.deinit(allocator);

    if (helper_protocol.reportsUnavailable(allocator, helper.stdout) catch false) {
        return .unavailable;
    }

    if (debug_latency) {
        reportOverlayLatency(io, launch_ts, helper.stderr);
    }

    const result = helper_protocol.parseSelectionEnvelope(allocator, helper.stdout, mode, constraint);
    const aspect_override = try helper_protocol.parseAspectOverrideAlloc(allocator, helper.stdout);
    defer aspect_override.deinit(allocator);
    const local_selection = try helper_protocol.parseConfirmedLocalSelectionAlloc(allocator, helper.stdout);
    errdefer if (local_selection) |local| local.deinit(allocator);

    const owned_stderr = if (debug_latency) try allocator.dupe(u8, helper.stderr) else null;
    errdefer if (owned_stderr) |s| allocator.free(s);

    return .{
        .selection = .{
            .result = result,
            .final_aspect_known = !result.cancelled,
            .final_aspect = if (!result.cancelled)
                try finalAspectForConfirmedSelection(allocator, helper_aspect, aspect_override)
            else
                null,
            .local_selection = local_selection,
            .debug_stderr = owned_stderr,
        },
    };
}

fn finalAspectForConfirmedSelection(
    allocator: std.mem.Allocator,
    helper_aspect: ?[]const u8,
    aspect_override: helper_protocol.AspectOverride,
) !?[]u8 {
    switch (aspect_override) {
        .free => return null,
        .value => |aspect| return try allocator.dupe(u8, aspect),
        .missing => {
            if (helper_aspect) |aspect| return try allocator.dupe(u8, aspect);
            return null;
        },
    }
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
    return compositor_focused_output.resolveName(allocator, io, environ);
}

fn overlayRuntimeDir(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    return runtime_paths.resolve(allocator, environ, null, "overlay");
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

/// Reports CLI-to-overlay-UI-visible latency when SHAULA_DEBUG_OVERLAY_LATENCY is set.
///
/// The helper writes `SHAULA_OVERLAY_READY_TS=<epoch_ms>` to stderr after
/// `gtk_window_present`; we parse it and compute the delta from the launch timestamp.
fn reportOverlayLatency(io: std.Io, launch_ts: i64, helper_stderr: []const u8) void {
    var stderr_buffer: [512]u8 = undefined;
    var stderr_writer = std.Io.File.stderr().writer(io, &stderr_buffer);
    const ready_ts = parseReadyTimestamp(helper_stderr) orelse {
        stderr_writer.interface.print("[DEBUG-overlay-latency] launch_ts={d}ms but no SHAULA_OVERLAY_READY_TS found in helper stderr\n", .{launch_ts}) catch {};
        stderr_writer.interface.flush() catch {};
        return;
    };
    const delta_ms = ready_ts - launch_ts;
    stderr_writer.interface.print("[DEBUG-overlay-latency] launch_to_ui_visible={d}ms (launch={d}, ready={d})\n", .{ delta_ms, launch_ts, ready_ts }) catch {};
    stderr_writer.interface.flush() catch {};
}

fn parseReadyTimestamp(helper_stderr: []const u8) ?i64 {
    const marker = "SHAULA_OVERLAY_READY_TS=";
    const start = std.mem.indexOf(u8, helper_stderr, marker) orelse return null;
    const value_start = start + marker.len;
    var value_end = value_start;
    while (value_end < helper_stderr.len and helper_stderr[value_end] != '\n' and helper_stderr[value_end] != '\r') {
        value_end += 1;
    }
    const value = helper_stderr[value_start..value_end];
    return std.fmt.parseInt(i64, value, 10) catch null;
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

test "confirmed helper aspect resolves override and fallback" {
    const custom_override: helper_protocol.AspectOverride = .{ .value = try std.testing.allocator.dupe(u8, "16:3") };
    defer custom_override.deinit(std.testing.allocator);
    const custom = try finalAspectForConfirmedSelection(std.testing.allocator, "16:9", custom_override);
    defer if (custom) |aspect| std.testing.allocator.free(aspect);
    try std.testing.expectEqualStrings("16:3", custom orelse return error.TestExpectedEqual);

    const fallback = try finalAspectForConfirmedSelection(std.testing.allocator, "4:3", .missing);
    defer if (fallback) |aspect| std.testing.allocator.free(aspect);
    try std.testing.expectEqualStrings("4:3", fallback orelse return error.TestExpectedEqual);

    const free = try finalAspectForConfirmedSelection(std.testing.allocator, "4:3", .free);
    try std.testing.expect(free == null);
}
