const std = @import("std");
const selection = @import("../selection/selection.zig");
const capture_session = @import("capture_session.zig");
const helper_protocol = @import("helper_protocol.zig");
const ui_state_store = @import("ui_state_store.zig");
const aspect_store = @import("aspect_store.zig");
const selection_draft_store = @import("selection_draft_store.zig");
const c = @cImport({
    @cInclude("core/capture_mode.h");
    @cInclude("runtime/paths.h");
    @cInclude("runtime/process_exec.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn pathSpan(value: []const u8) c.ShaulaRuntimePathSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn resolveRuntimePath(allocator: std.mem.Allocator, environ: std.process.Environ, relative_path: []const u8) ![]u8 {
    var owned: c.ShaulaRuntimeOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_owned_path_clear(&owned);
    const status = c.shaula_runtime_path_resolve(
        null,
        envValue(environ, "XDG_RUNTIME_DIR"),
        pathSpan(relative_path),
        &owned,
    );
    return switch (status) {
        c.SHAULA_RUNTIME_PATH_STATUS_OK => allocator.dupe(u8, owned.data[0..owned.length]),
        c.SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT => error.InvalidPath,
        c.SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        else => error.PathResolutionFailed,
    };
}
const overlay_runtime = @import("runtime.zig");
const compositor_focused_output = @import("../compositor/focused_output.zig");

pub const DraftMode = selection_draft_store.DraftMode;
pub const RegionCaptureMode = c.ShaulaRegionCaptureMode;
pub const ConfirmAction = helper_protocol.ConfirmAction;
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

pub const PreparedFrozenSource = struct {
    path: []u8,
    cleanup: bool,
    output_name: ?[]u8 = null,

    pub fn deinit(self: PreparedFrozenSource, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.cleanup) {
            std.Io.Dir.deleteFileAbsolute(io, self.path) catch {};
        }
        if (self.output_name) |name| allocator.free(name);
        allocator.free(self.path);
    }
};

pub const FrozenSource = struct {
    path: []u8,
    cleanup: bool,
    local_geometry: helper_protocol.LocalGeometry,
    surface_width: u32,
    surface_height: u32,

    pub fn deinit(self: FrozenSource, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.cleanup) {
            std.Io.Dir.deleteFileAbsolute(io, self.path) catch {};
        }
        allocator.free(self.path);
    }
};

pub const SelectionOutcome = struct {
    result: selection.SelectionResult,
    frozen_source: ?FrozenSource = null,
    confirm_action: helper_protocol.ConfirmAction = .capture,
    unavailable: bool = false,

    pub fn deinit(self: *SelectionOutcome, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.frozen_source) |source| source.deinit(allocator, io);
        self.frozen_source = null;
    }
};

/// Captures the frozen source image before the overlay helper is launched.
///
/// Contract constraint: frozen region capture must show and crop the same source
/// frame. This function is called by the capture lifecycle before opening the
/// helper window; failure returns `null` so the caller can emit deterministic
/// frozen-source backend errors.
pub fn prepareFrozenSourceForOverlay(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?PreparedFrozenSource {
    const output_name = try resolveOverlayOutputName(allocator, io, environ);
    errdefer if (output_name) |name| allocator.free(name);

    const background = try prepareOverlayBackground(allocator, io, environ, output_name);
    if (background) |prepared| {
        return .{
            .path = prepared.path,
            .cleanup = prepared.cleanup,
            .output_name = output_name,
        };
    }

    if (output_name) |name| allocator.free(name);
    return null;
}

/// Executes a complete overlay selection session.
///
/// Contract constraints:
/// - helper contract parsing failures are converted to deterministic
///   cancellation so caller boundaries emit stable `ERR_*` outcomes.
/// - accepted selections are the only path that persists draft and toolbar UI
///   state; runtime preparation and protocol parsing stay internal.
/// - a provided frozen source is passed into the helper as its visual
///   background and later promoted to the crop source for the confirmed area.
pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    interaction_mode: InteractionMode,
    draft_mode: DraftMode,
    constraint: selection.SelectionConstraint,
    region_capture_mode: RegionCaptureMode,
    prepared_frozen_source: *?PreparedFrozenSource,
    is_dry_run: bool,
    simulate_cancel: bool,
) !SelectionOutcome {
    if (simulate_cancel) {
        return .{ .result = cancelledSelection(mode, constraint) };
    }

    if (try helper_protocol.deterministicInteractionScenarioPayload(allocator, environ, constraint)) |payload| {
        defer allocator.free(payload);
        return .{ .result = helper_protocol.parseSelectionEnvelope(allocator, payload, mode, constraint) };
    }

    if (helper_protocol.testPayload(environ)) |payload| {
        return .{ .result = helper_protocol.parseSelectionEnvelope(allocator, payload, mode, constraint) };
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
        return .{ .result = result };
    }

    const helper_attempt = try runHelperSelectionAttempt(allocator, io, environ, mode, interaction_mode, draft_mode, constraint, region_capture_mode, prepared_frozen_source);
    switch (helper_attempt) {
        .selection => |helper_selection_raw| {
            var helper_selection = helper_selection_raw;
            defer helper_selection.deinit(allocator, io);
            const result = helper_selection.result;
            persistSelectionDraft(allocator, io, environ, draft_mode, result) catch {};
            persistOutputAwareSelectionDraft(allocator, io, environ, draft_mode, helper_selection) catch {};
            persistSelectionAspect(allocator, io, environ, interaction_mode, result, helper_selection) catch {};
            persistToolbarPositionForSelection(allocator, io, environ, result) catch {};
            const frozen_source = helper_selection.frozen_source;
            helper_selection.frozen_source = null;
            return .{ .result = result, .frozen_source = frozen_source, .confirm_action = helper_selection.confirm_action };
        },
        .unavailable => return .{ .result = cancelledSelection(mode, constraint), .unavailable = true },
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
    frozen_source: ?FrozenSource = null,
    confirm_action: helper_protocol.ConfirmAction = .capture,
    debug_stderr: ?[]const u8 = null,

    fn deinit(self: HelperSelection, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.final_aspect) |aspect| allocator.free(aspect);
        if (self.local_selection) |local| local.deinit(allocator);
        if (self.frozen_source) |source| source.deinit(allocator, io);
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
    prepared_frozen_source: *?PreparedFrozenSource,
) !HelperSelectionAttempt {
    const live_output_name = if (region_capture_mode == c.SHAULA_REGION_CAPTURE_MODE_LIVE)
        try resolveOverlayOutputName(allocator, io, environ)
    else
        null;
    defer if (live_output_name) |name| allocator.free(name);

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
    if (region_capture_mode == c.SHAULA_REGION_CAPTURE_MODE_FROZEN) {
        if (prepared_frozen_source.*) |prepared| {
            try helper_env.put("SHAULA_OVERLAY_BACKGROUND_PATH", prepared.path);
        }
    }
    if (prepared_frozen_source.*) |prepared| {
        if (prepared.output_name) |name| {
            try helper_env.put("SHAULA_OVERLAY_OUTPUT_NAME", name);
        }
    } else if (live_output_name) |name| {
        try helper_env.put("SHAULA_OVERLAY_OUTPUT_NAME", name);
    }
    const output_name_for_draft = if (prepared_frozen_source.*) |prepared| prepared.output_name else live_output_name;
    const initial = try selection_draft_store.loadInitialForOutputName(allocator, io, environ, draft_mode, output_name_for_draft);
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
    const confirm_action = helper_protocol.parseConfirmAction(allocator, helper.stdout) orelse .capture;
    const aspect_override = try helper_protocol.parseAspectOverrideAlloc(allocator, helper.stdout);
    defer aspect_override.deinit(allocator);
    const local_selection = try helper_protocol.parseConfirmedLocalSelectionAlloc(allocator, helper.stdout);
    errdefer if (local_selection) |local| local.deinit(allocator);
    const frozen_source =
        try frozenSourceForConfirmedSelection(allocator, prepared_frozen_source, result, local_selection);

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
            .frozen_source = frozen_source,
            .confirm_action = if (result.cancelled) .capture else confirm_action,
            .debug_stderr = owned_stderr,
        },
    };
}

fn frozenSourceForConfirmedSelection(
    allocator: std.mem.Allocator,
    prepared_source: *?PreparedFrozenSource,
    result: selection.SelectionResult,
    local_selection: ?helper_protocol.LocalSelection,
) !?FrozenSource {
    if (result.cancelled) return null;
    const prepared = prepared_source.* orelse return null;
    const local = local_selection orelse return null;

    prepared_source.* = null;
    if (prepared.output_name) |name| {
        // Output name ownership is only needed before helper launch.
        allocator.free(name);
    }
    return .{
        .path = prepared.path,
        .cleanup = prepared.cleanup,
        .local_geometry = local.geometry,
        .surface_width = local.output_width,
        .surface_height = local.output_height,
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
) !?PreparedFrozenSource {
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

    const spans = try allocator.alloc(c.ShaulaProcessSpan, argv.len);
    defer allocator.free(spans);
    for (argv, spans) |value, *span| span.* = processSpan(value);

    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    if (c.shaula_process_run(.{ .items = spans.ptr, .length = spans.len }, null, 1024, 1024, &output) != c.SHAULA_PROCESS_STATUS_OK or
        output.term_kind != c.SHAULA_PROCESS_TERM_EXITED or output.term_value != 0)
    {
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
    return resolveRuntimePath(allocator, environ, "overlay");
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

    var prepared_source: ?PreparedFrozenSource = null;
    var outcome = try runSelection(
        std.testing.allocator,
        std.testing.io,
        .{ .block = block },
        .freeform,
        .area,
        .area,
        .{ .aspect = null },
        c.SHAULA_REGION_CAPTURE_MODE_LIVE,
        &prepared_source,
        false,
        false,
    );
    defer outcome.deinit(std.testing.allocator, std.testing.io);

    const result = outcome.result;
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

    var prepared_source: ?PreparedFrozenSource = null;
    const attempt = try runHelperSelectionAttempt(
        std.testing.allocator,
        std.testing.io,
        .{ .block = block },
        .freeform,
        .area,
        .area,
        .{ .aspect = null },
        c.SHAULA_REGION_CAPTURE_MODE_LIVE,
        &prepared_source,
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
