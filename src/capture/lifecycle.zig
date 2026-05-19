const std = @import("std");

const capture_backend = @import("../backends/capture_backend.zig");
const capture_backend_failure = @import("../backends/capture_backend_failure.zig");
const capture_backend_output_path = @import("../backends/capture_backend_output_path.zig");
const capture_backend_png_meta = @import("../backends/capture_backend_png_meta.zig");
const capture_types = @import("types.zig");
const compositor_focused_output = @import("../compositor/focused_output.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const config_loader = @import("../config/loader.zig");
const config_types = @import("../config/config.zig");
const core_capture_mode = @import("../core/capture_mode.zig");
const flags = @import("command_flags.zig");
const guards = @import("command_guards.zig");
const invocation = @import("invocation.zig");
const json = @import("command_json.zig");
const overlay = @import("../overlay/overlay.zig");
const post_capture_pipeline = @import("../pipeline/post_capture.zig");
const capture_session_lock = @import("../runtime/capture_session_lock.zig");
const previous_area_store = @import("../runtime/previous_area_store.zig");
const process_exec = @import("../runtime/process_exec.zig");
const recovery_policy = @import("../recovery/policy.zig");
const selection = @import("../selection/selection.zig");

const CaptureSessionAcquire = struct {
    lock: ?capture_session_lock.CaptureSessionLock = null,
    exit_code: ?u8 = null,
};

const OverlayModeSpec = struct {
    interaction_mode: overlay.InteractionMode,
    draft_mode: overlay.DraftMode,
};

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
    session_lock: *capture_session_lock.CaptureSessionLock,
    frozen_source: ?overlay.FrozenSource,
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

    var loaded_config = config_loader.load(allocator, io, environ) catch null;
    defer if (loaded_config) |*loaded| loaded.deinit(allocator);
    const config = if (loaded_config) |loaded| loaded.config else config_types.Config{};
    var resolved_options = options;
    resolved_options.post_flags = resolvePostCaptureFlags(options.reported_mode, options.post_flags, config.capture.after);
    resolved_options.post_flags.show_success_notifications = config.notifications.success;
    resolved_options.post_flags.show_error_notifications = config.notifications.errors;
    resolved_options.post_flags.include_notification_thumbnail = config.notifications.thumbnails;

    const runtime = runtime_capabilities.resolve(environ);
    const focused_output_name = try resolveFocusedOutputForCapture(allocator, io, environ, runtime, options.request_mode);
    defer if (focused_output_name) |name| allocator.free(name);

    if (frozen_source != null and options.request_mode != .area) {
        return error.FrozenCaptureRequiresArea;
    }

    var outcome = if (frozen_source) |source|
        try executeFrozenSourceCapture(allocator, io, environ, options, resolved_options.post_flags.save, config.capture.after.save_folder.value(), source)
    else
        try capture_backend.execute(allocator, io, environ, .{
            .runtime = runtime,
            .focused_output_name = focused_output_name,
        }, .{
            .mode = options.request_mode,
            .output_path = options.output_path,
            .save_requested = resolved_options.post_flags.save,
            .save_folder = config.capture.after.save_folder.value(),
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

    session_lock.release();
    return writeCaptureOutcome(allocator, io, environ, resolved_options.command, resolved_options.reported_mode, &outcome, resolved_options.post_flags, precondition_warning);
}

fn resolvePostCaptureFlags(
    mode: []const u8,
    explicit: invocation.PostCaptureFlags,
    after: config_types.CaptureAfterConfig,
) invocation.PostCaptureFlags {
    const mode_after = if (std.mem.eql(u8, mode, "quick"))
        after.quick
    else if (std.mem.eql(u8, mode, "area"))
        after.area
    else if (std.mem.eql(u8, mode, "fullscreen"))
        after.fullscreen
    else if (std.mem.eql(u8, mode, "all-screens"))
        after.all_screens
    else
        config_types.CaptureAfterModeConfig{};

    const preview = if (explicit.preview_explicit)
        explicit.preview
    else
        !mode_after.skip_preview;

    return .{
        .preview = preview,
        .copy = explicit.copy or mode_after.copy_to_clipboard,
        .save = explicit.save or (!preview and mode_after.save_to_folder),
        .copy_explicit = explicit.copy_explicit,
        .save_explicit = explicit.save_explicit,
        .preview_explicit = explicit.preview_explicit,
        .show_success_notifications = true,
        .show_error_notifications = true,
        .include_notification_thumbnail = true,
    };
}

fn executeFrozenSourceCapture(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: invocation.Invocation,
    save_requested: bool,
    save_folder: ?[]const u8,
    source: overlay.FrozenSource,
) !capture_types.CaptureOutcome {
    const backend_used = "frozen-source";
    const output_path = capture_backend_output_path.resolveOutputPath(
        allocator,
        io,
        capture_types.modeString(options.request_mode),
        environ,
        options.output_path,
        save_requested,
        save_folder,
    ) catch |err| switch (err) {
        error.OutputPathInvalid => {
            return capture_backend_failure.outcome(capture_types, options.request_mode, "ERR_OUTPUT_PATH_INVALID", "output path is not writable", false, false, backend_used);
        },
        else => {
            return capture_backend_failure.unknown(capture_types, options.request_mode, backend_used, "frozen capture output path failed with unmapped error");
        },
    };
    errdefer allocator.free(output_path);

    if (std.fs.path.dirname(output_path)) |parent| {
        std.Io.Dir.cwd().createDirPath(io, parent) catch {
            allocator.free(output_path);
            return capture_backend_failure.outcome(capture_types, options.request_mode, "ERR_OUTPUT_PATH_INVALID", "output path is not writable", false, false, backend_used);
        };
    }

    logFrozenCapture(io, source, output_path);
    if (!try cropFrozenSource(allocator, io, environ, source, output_path)) {
        allocator.free(output_path);
        return capture_backend_failure.backendUnavailable(capture_types, options.request_mode, backend_used);
    }

    const dimensions_meta = capture_backend_png_meta.resolveCaptureDimensions(allocator, io, output_path) catch {
        allocator.free(output_path);
        return capture_backend_failure.backendUnavailable(capture_types, options.request_mode, backend_used);
    };

    return .{
        .success = .{
            .mode = options.request_mode,
            .path = output_path,
            .mime = "image/png",
            .dimensions = .{ .width = dimensions_meta.width, .height = dimensions_meta.height },
            .backend_used = backend_used,
            .latency_ms = 0,
            .degraded = false,
        },
    };
}

fn cropFrozenSource(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    source: overlay.FrozenSource,
    output_path: []const u8,
) !bool {
    const helper_bin = try resolveCropHelperBinary(allocator, io, environ);
    defer allocator.free(helper_bin);

    const x = try std.fmt.allocPrint(allocator, "{d}", .{source.local_geometry.x});
    defer allocator.free(x);
    const y = try std.fmt.allocPrint(allocator, "{d}", .{source.local_geometry.y});
    defer allocator.free(y);
    const width = try std.fmt.allocPrint(allocator, "{d}", .{source.local_geometry.width});
    defer allocator.free(width);
    const height = try std.fmt.allocPrint(allocator, "{d}", .{source.local_geometry.height});
    defer allocator.free(height);
    const surface_width = try std.fmt.allocPrint(allocator, "{d}", .{source.surface_width});
    defer allocator.free(surface_width);
    const surface_height = try std.fmt.allocPrint(allocator, "{d}", .{source.surface_height});
    defer allocator.free(surface_height);

    const result = process_exec.run(
        allocator,
        io,
        &.{ helper_bin, source.path, output_path, x, y, width, height, surface_width, surface_height },
        1024,
        2048,
    ) catch return false;
    defer result.deinit(allocator);

    return result.exitedZero();
}

fn resolveCropHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_CROP_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-crop-image");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-crop-image", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-crop-image");
    }
}

fn logFrozenCapture(io: std.Io, source: overlay.FrozenSource, output_path: []const u8) void {
    var stderr_buffer: [512]u8 = undefined;
    var stderr_writer = std.Io.File.stderr().writer(io, &stderr_buffer);
    stderr_writer.interface.print(
        "[shaula-frozen-capture] crop source={s} output={s} rect={d},{d} {d}x{d} surface={d}x{d}\n",
        .{
            source.path,
            output_path,
            source.local_geometry.x,
            source.local_geometry.y,
            source.local_geometry.width,
            source.local_geometry.height,
            source.surface_width,
            source.surface_height,
        },
    ) catch {};
    stderr_writer.interface.flush() catch {};
}

fn resolveFocusedOutputForCapture(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    runtime: runtime_capabilities.RuntimeDecision,
    mode: capture_types.CaptureMode,
) !?[]u8 {
    if (!runtime.compositor_supported) return null;
    if (mode != .fullscreen and mode != .focused) return null;
    return compositor_focused_output.resolveName(allocator, io, environ);
}

/// Execute `capture all-in-one` through the shared capture lifecycle.
pub fn runAllInOne(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAllInOneFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, .all_in_one, parsed);
}

/// Execute `capture quick` through the capture-on-release overlay lifecycle.
pub fn runQuick(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseQuickFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, .quick, parsed);
}

/// Execute `capture area` through the shared capture lifecycle.
pub fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, .area, parsed);
}

pub fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, .fullscreen, parsed);
}

pub fn runAllScreens(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAllScreensFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, .all_screens, parsed);
}

pub fn runFocused(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseFocusedFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, .focused, parsed);
}

pub fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, .window, parsed);
}

fn runOverlayMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    comptime mode: core_capture_mode.CaptureMode,
    parsed: anytype,
) !u8 {
    const reported_mode = core_capture_mode.cliToken(mode);
    const command = commandForMode(mode);

    const capture_session = try beginJsonCaptureSession(allocator, io, environ, command, reported_mode, parsed.json_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    const region_capture_mode = resolveRegionCaptureMode(allocator, io, environ, parsed.region_capture_mode);
    if (!parsed.dry_run) {
        const backend_mode = core_capture_mode.backendModeToken(mode) orelse reported_mode;
        const unsupported_rc = try guards.enforceModeSupported(io, environ, command, backend_mode);
        if (unsupported_rc) |code| return code;
    }

    var prepared_frozen_source: ?overlay.PreparedFrozenSource = null;
    defer if (prepared_frozen_source) |source| source.deinit(allocator, io);
    if (region_capture_mode == .frozen and !parsed.dry_run) {
        prepared_frozen_source = try overlay.prepareFrozenSourceForOverlay(allocator, io, environ);
    }

    const overlay_spec = overlaySpecForMode(mode);
    var selection_result = try resolveOverlaySelection(
        allocator,
        io,
        environ,
        command,
        reported_mode,
        overlay_spec.interaction_mode,
        overlay_spec.draft_mode,
        parsed.aspect,
        region_capture_mode,
        &prepared_frozen_source,
        parsed.dry_run,
        parsed.simulate_cancel,
    );
    defer selection_result.deinit(allocator, io);
    if (selection_result.exit_code) |code| return code;
    const selected = selection_result.selection;

    if (parsed.dry_run) {
        if (mode == .area) {
            try json.writeAreaDryRunJson(allocator, io, selected);
        } else {
            try json.writeSelectionDryRunJson(allocator, io, command, selected);
        }
        return 0;
    }

    if (region_capture_mode == .frozen and selection_result.frozen_source == null) {
        try json.writeErrorJson(
            io,
            command,
            "ERR_CAPTURE_BACKEND_UNAVAILABLE",
            "frozen capture source unavailable",
            true,
            reported_mode,
            null,
            false,
            &.{"frozen_source_missing"},
        );
        return recovery_policy.exitCodeFor("ERR_CAPTURE_BACKEND_UNAVAILABLE");
    }

    const geometry = capture_types.areaGeometryFromSelection(selected.geometry);
    const options = switch (mode) {
        .quick => invocation.quick(parsed, region_capture_mode, geometry),
        .area => invocation.area(parsed, region_capture_mode, geometry),
        .all_in_one => invocation.allInOne(parsed, region_capture_mode, geometry),
        else => unreachable,
    };
    return executeLifecycle(allocator, io, environ, options, &session_lock, selection_result.frozen_source);
}

fn runDirectMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    comptime mode: core_capture_mode.CaptureMode,
    parsed: anytype,
) !u8 {
    const reported_mode = core_capture_mode.cliToken(mode);
    const command = commandForMode(mode);

    const capture_session = try beginJsonCaptureSession(allocator, io, environ, command, reported_mode, parsed.json_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    const options = switch (mode) {
        .fullscreen => invocation.fullscreen(parsed),
        .all_screens => invocation.allScreens(parsed),
        .focused => invocation.focused(parsed),
        .window => invocation.window(parsed),
        else => unreachable,
    };
    return executeLifecycle(allocator, io, environ, options, &session_lock, null);
}

/// Execute `capture previous-area` without fabricating missing geometry.
pub fn runPreviousArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const reported_mode = core_capture_mode.cliToken(.previous_area);
    const backend_mode = core_capture_mode.backendModeToken(.previous_area) orelse reported_mode;
    const parsed = flags.parsePreviousAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (try validateJsonMode(io, "capture previous-area", reported_mode, parsed.json_mode)) |code| return code;

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture previous-area", backend_mode);
    if (unsupported_rc) |code| return code;

    const geometry = (try previous_area_store.load(allocator, io, environ)) orelse {
        try json.writeErrorJson(io, "capture previous-area", "ERR_PREVIOUS_AREA_UNAVAILABLE", "previous area is unavailable", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_PREVIOUS_AREA_UNAVAILABLE");
    };

    const capture_session = try beginCaptureSession(allocator, io, environ, "capture previous-area", reported_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    return executeLifecycle(allocator, io, environ, invocation.previousArea(parsed, geometry), &session_lock, null);
}

/// Starts the capture-only session gate used by compositor shortcuts.
///
/// Contract: `ERR_CAPTURE_IN_PROGRESS` is deterministic and retryable; callers
/// release the returned lock before preview so open previews do not block newer
/// captures.
fn beginCaptureSession(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
) !CaptureSessionAcquire {
    const lock = capture_session_lock.acquire(allocator, io, environ) catch |err| switch (err) {
        error.CaptureInProgress => {
            try json.writeErrorJson(
                io,
                command,
                "ERR_CAPTURE_IN_PROGRESS",
                "another capture is already in progress",
                true,
                reported_mode,
                null,
                false,
                &.{"capture_session_busy"},
            );
            return .{ .exit_code = recovery_policy.exitCodeFor("ERR_CAPTURE_IN_PROGRESS") };
        },
        else => return err,
    };

    return .{ .lock = lock };
}

/// Validates JSON-only capture commands and acquires the capture session gate.
///
/// Contract constraint: this preamble preserves the public `ERR_CLI_USAGE`
/// shape before lock acquisition, then maps lock contention to deterministic
/// `ERR_CAPTURE_IN_PROGRESS`.
fn beginJsonCaptureSession(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    json_mode: bool,
) !CaptureSessionAcquire {
    if (try validateJsonMode(io, command, reported_mode, json_mode)) |code| return .{ .exit_code = code };

    return beginCaptureSession(allocator, io, environ, command, reported_mode);
}

fn validateJsonMode(io: std.Io, command: []const u8, reported_mode: []const u8, json_mode: bool) !?u8 {
    if (json_mode) return null;
    try json.writeErrorJson(io, command, "ERR_CLI_USAGE", "--json is required", false, reported_mode, null, false, &.{});
    return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
}

fn commandForMode(comptime mode: core_capture_mode.CaptureMode) []const u8 {
    return switch (mode) {
        .quick => "capture quick",
        .area => "capture area",
        .fullscreen => "capture fullscreen",
        .all_screens => "capture all-screens",
        .focused => "capture focused",
        .window => "capture window",
        .previous_area => "capture previous-area",
        .all_in_one => "capture all-in-one",
    };
}

fn overlaySpecForMode(comptime mode: core_capture_mode.CaptureMode) OverlayModeSpec {
    return switch (mode) {
        .quick => .{ .interaction_mode = .quick, .draft_mode = .quick },
        .area => .{ .interaction_mode = .area, .draft_mode = .area },
        .all_in_one => .{ .interaction_mode = .quick, .draft_mode = .capture },
        else => unreachable,
    };
}

const OverlaySelectionOutcome = struct {
    selection: selection.SelectionResult,
    frozen_source: ?overlay.FrozenSource = null,
    exit_code: ?u8 = null,

    fn deinit(self: *OverlaySelectionOutcome, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.frozen_source) |source| source.deinit(allocator, io);
        self.frozen_source = null;
    }
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
    prepared_frozen_source: *?overlay.PreparedFrozenSource,
    dry_run: bool,
    simulate_cancel: bool,
) !OverlaySelectionOutcome {
    const force_noninteractive_selection = envFlagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = dry_run or force_noninteractive_selection;
    var result = try overlay.runSelection(
        allocator,
        io,
        environ,
        selection.SelectionMode.freeform,
        interaction_mode,
        draft_mode,
        .{ .aspect = aspect },
        region_capture_mode,
        prepared_frozen_source,
        use_overlay_dry_run,
        simulate_cancel,
    );
    defer result.deinit(allocator, io);
    if (!result.result.cancelled) {
        const frozen_source = result.frozen_source;
        result.frozen_source = null;
        return .{ .selection = result.result, .frozen_source = frozen_source };
    }

    const overlay_code = overlay.deterministicFailureCode(environ, simulate_cancel, true);
    if (overlay_code) |code| {
        const spec = recovery_policy.specFor(code);
        try json.writeErrorJson(io, command, spec.code, spec.message, spec.retryable, reported_mode, null, false, &.{});
        return .{ .selection = result.result, .exit_code = recovery_policy.exitCodeFor(spec.code) };
    }

    try json.writeErrorJson(io, command, "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, reported_mode, null, false, &.{});
    return .{ .selection = result.result, .exit_code = recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED") };
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
