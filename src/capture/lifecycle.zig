const std = @import("std");

const capture_backend = @import("../backends/capture_backend.zig");
const capture_types = @import("types.zig");
const config_loader = @import("../config/loader.zig");
const core_capture_mode = @import("../core/capture_mode.zig");
const flags = @import("command_flags.zig");
const guards = @import("command_guards.zig");
const invocation = @import("invocation.zig");
const json = @import("command_json.zig");
const overlay = @import("../overlay/overlay.zig");
const post_capture_pipeline = @import("../pipeline/post_capture.zig");
const previous_area_store = @import("../runtime/previous_area_store.zig");
const recovery_policy = @import("../recovery/policy.zig");
const selection = @import("../selection/selection.zig");

/// Execute the shared capture lifecycle after mode-specific inputs are resolved.
///
/// Contract constraints:
/// - `ERR_*` capability, precondition, backend, and post-capture mappings stay
///   centralized here.
/// - callers provide the mode-specific backend request and optional previous
///   area persistence; this Module owns the ordering.
fn executeLifecycle(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: invocation.Invocation,
) !u8 {
    const unsupported_rc = try guards.enforceModeSupported(io, environ, options.command, options.backend_mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, options.command, options.backend_mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };
    if (options.settle_region_mode) |region_capture_mode| {
        settleAfterLiveOverlay(io, environ, region_capture_mode);
    }

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = options.request_mode,
        .output_path = options.output_path,
        .save_requested = options.post_flags.save,
        .window_id = options.window_id,
        .area_geometry = options.area_geometry,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    if (options.persist_previous_area) |geometry| {
        switch (outcome) {
            .success => previous_area_store.store(allocator, io, environ, geometry) catch {},
            .failure => {},
        }
    }

    return writeCaptureOutcome(allocator, io, environ, options.command, options.reported_mode, &outcome, options.post_flags, precondition_warning);
}

/// Execute `capture all-in-one` through the shared capture lifecycle.
pub fn runAllInOne(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const reported_mode = core_capture_mode.cliToken(.all_in_one);
    const backend_mode = core_capture_mode.backendModeToken(.all_in_one) orelse reported_mode;
    const parsed = flags.parseAllInOneFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "--json is required", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const region_capture_mode = resolveRegionCaptureMode(allocator, io, environ, parsed.region_capture_mode);
    const selection_result = try resolveOverlaySelection(allocator, io, environ, "capture all-in-one", reported_mode, .quick, .capture, parsed.aspect, region_capture_mode, parsed.dry_run, parsed.simulate_cancel);
    if (selection_result.exit_code) |code| return code;
    const selected = selection_result.selection;

    if (parsed.dry_run) {
        try json.writeSelectionDryRunJson(allocator, io, "capture all-in-one", selected);
        return 0;
    }

    const geometry = capture_types.areaGeometryFromSelection(selected.geometry);
    _ = backend_mode;
    return executeLifecycle(allocator, io, environ, invocation.allInOne(parsed, region_capture_mode, geometry));
}

/// Execute `capture quick` through the capture-on-release overlay lifecycle.
pub fn runQuick(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.quick);
    const parsed = flags.parseQuickFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture quick", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const region_capture_mode = resolveRegionCaptureMode(allocator, io, environ, parsed.region_capture_mode);
    const selection_result = try resolveOverlaySelection(allocator, io, environ, "capture quick", mode, .quick, .quick, parsed.aspect, region_capture_mode, parsed.dry_run, parsed.simulate_cancel);
    if (selection_result.exit_code) |code| return code;
    const selected = selection_result.selection;

    if (parsed.dry_run) {
        try json.writeSelectionDryRunJson(allocator, io, "capture quick", selected);
        return 0;
    }

    const geometry = capture_types.areaGeometryFromSelection(selected.geometry);
    return executeLifecycle(allocator, io, environ, invocation.quick(parsed, region_capture_mode, geometry));
}

/// Execute `capture area` through the shared capture lifecycle.
pub fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.area);
    const parsed = flags.parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const region_capture_mode = resolveRegionCaptureMode(allocator, io, environ, parsed.region_capture_mode);
    const selection_result = try resolveOverlaySelection(allocator, io, environ, "capture area", mode, .area, .area, parsed.aspect, region_capture_mode, parsed.dry_run, parsed.simulate_cancel);
    if (selection_result.exit_code) |code| return code;
    const selected = selection_result.selection;

    if (parsed.dry_run) {
        try json.writeAreaDryRunJson(allocator, io, selected);
        return 0;
    }

    const geometry = capture_types.areaGeometryFromSelection(selected.geometry);
    return executeLifecycle(allocator, io, environ, invocation.area(parsed, region_capture_mode, geometry));
}

pub fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.fullscreen);
    const parsed = flags.parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    return executeLifecycle(allocator, io, environ, invocation.fullscreen(parsed));
}

pub fn runAllScreens(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.all_screens);
    const parsed = flags.parseAllScreensFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture all-screens", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    return executeLifecycle(allocator, io, environ, invocation.allScreens(parsed));
}

pub fn runFocused(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.focused);
    const parsed = flags.parseFocusedFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture focused", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    return executeLifecycle(allocator, io, environ, invocation.focused(parsed));
}

pub fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.window);
    const parsed = flags.parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    return executeLifecycle(allocator, io, environ, invocation.window(parsed));
}

/// Execute `capture previous-area` without fabricating missing geometry.
pub fn runPreviousArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const reported_mode = core_capture_mode.cliToken(.previous_area);
    const backend_mode = core_capture_mode.backendModeToken(.previous_area) orelse reported_mode;
    const parsed = flags.parsePreviousAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture previous-area", "ERR_CLI_USAGE", "--json is required", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture previous-area", backend_mode);
    if (unsupported_rc) |code| return code;

    const geometry = (try previous_area_store.load(allocator, io, environ)) orelse {
        try json.writeErrorJson(io, "capture previous-area", "ERR_PREVIOUS_AREA_UNAVAILABLE", "previous area is unavailable", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_PREVIOUS_AREA_UNAVAILABLE");
    };

    return executeLifecycle(allocator, io, environ, invocation.previousArea(parsed, geometry));
}

const OverlaySelectionOutcome = struct {
    selection: selection.SelectionResult,
    exit_code: ?u8 = null,
};

fn resolveOverlaySelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    interaction_mode: overlay.InteractionMode,
    draft_mode: overlay.DraftMode,
    aspect: ?[]const u8,
    region_capture_mode: core_capture_mode.RegionCaptureMode,
    dry_run: bool,
    simulate_cancel: bool,
) !OverlaySelectionOutcome {
    const force_noninteractive_selection = envFlagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = dry_run or force_noninteractive_selection;
    const result = try overlay.runSelection(
        allocator,
        io,
        environ,
        selection.SelectionMode.freeform,
        interaction_mode,
        draft_mode,
        .{ .aspect = aspect },
        region_capture_mode,
        use_overlay_dry_run,
        simulate_cancel,
    );
    if (!result.cancelled) return .{ .selection = result };

    const overlay_code = overlay.deterministicFailureCode(environ, simulate_cancel, true);
    if (overlay_code) |code| {
        const spec = recovery_policy.specFor(code);
        try json.writeErrorJson(io, command, spec.code, spec.message, spec.retryable, reported_mode, null, false, &.{});
        return .{ .selection = result, .exit_code = recovery_policy.exitCodeFor(spec.code) };
    }

    try json.writeErrorJson(io, command, "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, reported_mode, null, false, &.{});
    return .{ .selection = result, .exit_code = recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED") };
}

fn writeCaptureOutcome(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    outcome: *capture_types.CaptureOutcome,
    post_flags: invocation.PostCaptureFlags,
    precondition_warning: ?[]const u8,
) !u8 {
    switch (outcome.*) {
        .success => |success| {
            var warnings: [2][]const u8 = undefined;
            var warning_count: usize = 0;
            if (precondition_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (success.degraded) {
                warnings[warning_count] = "capture_backend_degraded";
                warning_count += 1;
            }

            try post_capture_pipeline.writeCapturePipelineJson(allocator, io, environ, command, reported_mode, success, .{
                .save = post_flags.save,
                .copy = post_flags.copy,
                .preview = post_flags.preview,
            }, warnings[0..warning_count]);
            return 0;
        },
        .failure => |failure| {
            var warnings: [2][]const u8 = undefined;
            var warning_count: usize = 0;
            if (precondition_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (failure.degraded) {
                warnings[warning_count] = "window_capture_degraded";
                warning_count += 1;
            }

            try json.writeErrorJson(
                io,
                command,
                failure.code,
                failure.message,
                failure.retryable,
                reported_mode,
                failure.backend_used,
                failure.degraded,
                warnings[0..warning_count],
            );
            return recovery_policy.exitCodeFor(failure.code);
        },
    }
}

fn envFlagEnabled(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
    }
    return false;
}

fn resolveRegionCaptureMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    explicit: ?[]const u8,
) core_capture_mode.RegionCaptureMode {
    if (explicit) |token| {
        return core_capture_mode.parseRegionCaptureMode(token) orelse .live;
    }
    if (environ.getPosix("SHAULA_REGION_CAPTURE_MODE")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (core_capture_mode.parseRegionCaptureMode(raw)) |mode| return mode;
    }

    var loaded = config_loader.load(allocator, io, environ) catch return .live;
    defer loaded.deinit(allocator);
    return loaded.config.capture.region_capture_mode;
}

/// Gives Wayland/Niri one redraw opportunity after the live overlay exits.
fn settleAfterLiveOverlay(
    io: std.Io,
    environ: std.process.Environ,
    region_capture_mode: core_capture_mode.RegionCaptureMode,
) void {
    if (region_capture_mode != .live) return;

    const default_ms: u64 = 50;
    const settle_ms = if (environ.getPosix("SHAULA_LIVE_REGION_SETTLE_MS")) |raw_z| blk: {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        break :blk std.fmt.parseInt(u64, raw, 10) catch default_ms;
    } else default_ms;
    if (settle_ms == 0) return;
    const millis_i64: i64 = @intCast(settle_ms);
    const duration: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(millis_i64), .clock = .real };
    duration.sleep(io) catch {};
}
