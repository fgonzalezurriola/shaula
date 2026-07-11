const std = @import("std");

const capture_backend = @import("backends/capture_backend.zig");
const backend_contract = @import("backends/capture_backend_contract.zig");
const capture_backend_failure = @import("backends/capture_backend_failure.zig");
const capture_backend_output_path = @import("backends/capture_backend_output_path.zig");
const capture_backend_png_meta = @import("backends/capture_backend_png_meta.zig");
const capture_types = @import("types.zig");
const compositor_focused_output = @import("../compositor/focused_output.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const config_loader = @import("../config/loader.zig");
const config_types = @import("../config/config.zig");
const flags = @import("command_flags.zig");
const guards = @import("command_guards.zig");
const invocation = @import("invocation.zig");
const json = @import("command_json.zig");
const overlay_session = @import("../overlay/selection_session.zig");
const overlay_draft_store = @import("../overlay/selection_draft_store.zig");
const post_capture_pipeline = @import("post_capture.zig");
const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};
const c = @cImport({
    @cInclude("core/capture_mode.h");
    @cInclude("errors/taxonomy.h");
    @cInclude("runtime/env.h");
    @cInclude("runtime/paths.h");
    @cInclude("runtime/previous_area_store.h");
    @cInclude("runtime/capture_session_lock.h");
    @cInclude("runtime/process_exec.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn pathSpan(value: []const u8) c.ShaulaRuntimePathSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn captureModeSpan(value: []const u8) c.ShaulaCaptureModeSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn requiredCaptureModeSpan(value: c.ShaulaCaptureModeSpan) []const u8 {
    if (value.data == null) unreachable;
    return value.data[0..value.length];
}

fn optionalCaptureModeSpan(value: c.ShaulaCaptureModeSpan) ?[]const u8 {
    if (value.data == null) return null;
    return value.data[0..value.length];
}

fn captureModeToken(mode: c.ShaulaCaptureMode) []const u8 {
    return requiredCaptureModeSpan(c.shaula_capture_mode_cli_token(mode));
}

fn captureModeBackendToken(mode: c.ShaulaCaptureMode) ?[]const u8 {
    return optionalCaptureModeSpan(c.shaula_capture_mode_backend_token(mode));
}

fn parseRegionCaptureMode(token: []const u8) ?c.ShaulaRegionCaptureMode {
    const mode = c.shaula_region_capture_mode_parse(captureModeSpan(token));
    return if (mode == c.SHAULA_REGION_CAPTURE_MODE_INVALID) null else mode;
}

fn resolveRuntimePath(
    allocator: std.mem.Allocator,
    environ: std.process.Environ,
    override_key: []const u8,
    relative_path: []const u8,
) ![]u8 {
    var owned: c.ShaulaRuntimeOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_owned_path_clear(&owned);
    const status = c.shaula_runtime_path_resolve(
        envValue(environ, override_key),
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

const env = struct {
    fn trimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
        var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
        if (c.shaula_env_value_trimmed(envValue(environ, key), &result) != c.SHAULA_ENV_STATUS_VALID) return null;
        return result.data[0..result.length];
    }

    fn flagEnabled(environ: std.process.Environ, key: []const u8) bool {
        var value: i32 = 0;
        return c.shaula_env_value_flag(envValue(environ, key), &value) == c.SHAULA_ENV_STATUS_VALID and value != 0;
    }

    fn unsignedOrDefault(comptime Int: type, environ: std.process.Environ, key: []const u8, default_value: Int) Int {
        const info = switch (@typeInfo(Int)) {
            .int => |value| value,
            else => @compileError("unsignedOrDefault requires an unsigned integer type"),
        };
        comptime {
            if (info.signedness != .unsigned or info.bits > 64) @compileError("unsupported unsigned integer type");
        }
        return @intCast(c.shaula_env_value_unsigned_or_default(
            envValue(environ, key),
            @intCast(std.math.maxInt(Int)),
            @intCast(default_value),
        ));
    }
};

const previous_area_store = struct {
    fn span(value: []const u8) c.ShaulaPreviousAreaSpan {
        return .{ .data = value.ptr, .length = value.len };
    }

    fn geometry(value: capture_types.AreaGeometry) c.ShaulaPreviousAreaGeometry {
        return .{ .x = value.x, .y = value.y, .width = value.width, .height = value.height };
    }

    fn store(
        allocator: std.mem.Allocator,
        io: std.Io,
        environ: std.process.Environ,
        value: capture_types.AreaGeometry,
    ) !void {
        _ = io;
        const path = try resolveRuntimePath(allocator, environ, "SHAULA_PREVIOUS_AREA_FILE", "selection/previous-area.v1");
        defer allocator.free(path);
        return switch (c.shaula_previous_area_store(span(path), geometry(value))) {
            c.SHAULA_PREVIOUS_AREA_STATUS_OK => {},
            c.SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT => error.InvalidPath,
            c.SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
            else => error.PreviousAreaStoreFailed,
        };
    }

    fn load(
        allocator: std.mem.Allocator,
        io: std.Io,
        environ: std.process.Environ,
    ) !?capture_types.AreaGeometry {
        _ = io;
        const path = try resolveRuntimePath(allocator, environ, "SHAULA_PREVIOUS_AREA_FILE", "selection/previous-area.v1");
        defer allocator.free(path);
        var present: i32 = 0;
        var value: c.ShaulaPreviousAreaGeometry = .{ .x = 0, .y = 0, .width = 0, .height = 0 };
        return switch (c.shaula_previous_area_load(span(path), &present, &value)) {
            c.SHAULA_PREVIOUS_AREA_STATUS_OK => if (present != 0) capture_types.AreaGeometry{
                .x = value.x,
                .y = value.y,
                .width = value.width,
                .height = value.height,
            } else null,
            c.SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT => error.InvalidPath,
            else => null,
        };
    }

    fn supportedForBackendLabel(label: []const u8) bool {
        return c.shaula_previous_area_supported_for_backend(span(label)) != 0;
    }
};

const capture_session_lock = struct {
    const CaptureSessionLock = struct {
        allocator: std.mem.Allocator,
        path: []u8,
        active: bool = true,

        fn release(self: *CaptureSessionLock) void {
            if (!self.active) return;
            const span: c.ShaulaCaptureSessionSpan = .{ .data = self.path.ptr, .length = self.path.len };
            c.shaula_capture_session_lock_release(span);
            self.active = false;
        }

        fn deinit(self: *CaptureSessionLock) void {
            self.release();
            self.allocator.free(self.path);
        }
    };

    fn acquire(
        allocator: std.mem.Allocator,
        io: std.Io,
        environ: std.process.Environ,
    ) !CaptureSessionLock {
        _ = io;
        const path = try resolveRuntimePath(allocator, environ, "SHAULA_CAPTURE_SESSION_LOCK_FILE", "capture/session.lock");
        errdefer allocator.free(path);
        const span: c.ShaulaCaptureSessionSpan = .{ .data = path.ptr, .length = path.len };
        switch (c.shaula_capture_session_lock_acquire(span)) {
            c.SHAULA_CAPTURE_SESSION_STATUS_OK => {},
            c.SHAULA_CAPTURE_SESSION_STATUS_BUSY => return error.CaptureInProgress,
            c.SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT => return error.InvalidPath,
            c.SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY => return error.OutOfMemory,
            else => return error.CaptureSessionLockFailed,
        }
        return .{ .allocator = allocator, .path = path };
    }
};

const process_exec = struct {
    const ProcessOutput = struct {
        output: c.ShaulaProcessOutput,

        fn deinit(self: ProcessOutput, allocator: std.mem.Allocator) void {
            _ = allocator;
            var output = self.output;
            c.shaula_process_output_clear(&output);
        }

        fn exitedZero(self: ProcessOutput) bool {
            return self.output.term_kind == c.SHAULA_PROCESS_TERM_EXITED and self.output.term_value == 0;
        }
    };

    fn run(
        allocator: std.mem.Allocator,
        io: std.Io,
        argv: []const []const u8,
        stdout_limit: usize,
        stderr_limit: usize,
    ) !ProcessOutput {
        _ = io;
        const spans = try allocator.alloc(c.ShaulaProcessSpan, argv.len);
        defer allocator.free(spans);
        for (argv, spans) |value, *span| span.* = .{ .data = value.ptr, .length = value.len };
        var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
        errdefer c.shaula_process_output_clear(&output);
        if (c.shaula_process_run(
            .{ .items = spans.ptr, .length = spans.len },
            null,
            stdout_limit,
            stderr_limit,
            &output,
        ) != c.SHAULA_PROCESS_STATUS_OK) return error.ProcessFailed;
        return .{ .output = output };
    }
};
const selection = @import("../selection/selection.zig");
const warning_tokens = @import("warnings.zig");

const CaptureSessionAcquire = struct {
    lock: ?capture_session_lock.CaptureSessionLock = null,
    exit_code: ?u8 = null,
};

const OverlayModeSpec = struct {
    interaction_mode: overlay_session.InteractionMode,
    draft_mode: overlay_draft_store.DraftMode,
};

/// Execute the shared capture lifecycle after mode-specific inputs are resolved.
///
/// Contract constraints:
/// - `ERR_*` capability, precondition, backend, and post-capture mappings stay
///   centralized here.
/// - callers provide the mode-specific backend request and optional previous
///   area persistence; this Module owns the ordering.
fn executeLifecycle(
    runtime: runtime_capabilities.RuntimeDecision,
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: invocation.Invocation,
    session_lock: *capture_session_lock.CaptureSessionLock,
    frozen_source: ?overlay_session.FrozenSource,
    selection_warning: ?[]const u8,
) !u8 {
    const unsupported_rc = try guards.enforceModeSupported(runtime, io, options.command, options.backend_mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(runtime, allocator, io, environ, options.command, options.backend_mode) catch |err| switch (err) {
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
    return writeCaptureOutcome(allocator, io, environ, resolved_options.command, resolved_options.reported_mode, &outcome, resolved_options.post_flags, precondition_warning, selection_warning);
}

fn applyOverlayConfirmAction(
    post_flags: *invocation.PostCaptureFlags,
    action: overlay_session.ConfirmAction,
) void {
    switch (action) {
        .capture => {},
        .copy => {
            post_flags.copy = true;
            post_flags.copy_explicit = true;
            post_flags.preview = false;
            post_flags.preview_explicit = true;
        },
        .save => {
            post_flags.save = true;
            post_flags.save_explicit = true;
            post_flags.preview = false;
            post_flags.preview_explicit = true;
        },
    }
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
    source: overlay_session.FrozenSource,
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
    source: overlay_session.FrozenSource,
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

fn logFrozenCapture(io: std.Io, source: overlay_session.FrozenSource, output_path: []const u8) void {
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
pub fn runAllInOne(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAllInOneFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_ALL_IN_ONE, parsed);
}

/// Execute `capture quick` through the capture-on-release overlay lifecycle.
pub fn runQuick(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseQuickFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_QUICK, parsed);
}

/// Execute `capture area` through the shared capture lifecycle.
pub fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runOverlayMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_AREA, parsed);
}

pub fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_FULLSCREEN, parsed);
}

pub fn runAllScreens(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAllScreensFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_ALL_SCREENS, parsed);
}

pub fn runFocused(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseFocusedFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_FOCUSED, parsed);
}

pub fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    return runDirectMode(allocator, io, environ, runtime, c.SHAULA_CAPTURE_MODE_WINDOW, parsed);
}

fn runOverlayMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    runtime_arg: runtime_capabilities.RuntimeDecision,
    comptime mode: c.ShaulaCaptureMode,
    parsed: anytype,
) !u8 {
    var runtime = runtime_arg;
    const reported_mode = captureModeToken(mode);
    const command = commandForMode(mode);

    const capture_session = try beginJsonCaptureSession(allocator, io, environ, command, reported_mode, parsed.json_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    const region_capture_mode = resolveRegionCaptureMode(allocator, io, environ, parsed.region_capture_mode);
    if (!parsed.dry_run) {
        const backend_mode = captureModeBackendToken(mode) orelse reported_mode;
        const unsupported_rc = try guards.enforceModeSupported(runtime, io, command, backend_mode);
        if (unsupported_rc) |code| return code;

        if (runtime.shouldBypassOverlaySelection()) {
            const options = switch (mode) {
                c.SHAULA_CAPTURE_MODE_QUICK => invocation.quick(parsed, region_capture_mode, null),
                c.SHAULA_CAPTURE_MODE_AREA => invocation.area(parsed, region_capture_mode, null),
                c.SHAULA_CAPTURE_MODE_ALL_IN_ONE => invocation.allInOne(parsed, region_capture_mode, null),
                else => unreachable,
            };
            return executeLifecycle(runtime, allocator, io, environ, options, &session_lock, null, warning_tokens.selection_portal);
        }
    }

    var prepared_frozen_source: ?overlay_session.PreparedFrozenSource = null;
    defer if (prepared_frozen_source) |source| source.deinit(allocator, io);
    if (region_capture_mode == c.SHAULA_REGION_CAPTURE_MODE_FROZEN and !parsed.dry_run) {
        prepared_frozen_source = try overlay_session.prepareFrozenSourceForOverlay(allocator, io, environ);
    }

    const overlay_spec = overlaySpecForMode(mode);
    var selection_result = try resolveOverlaySelection(
        runtime,
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

    if (selection_result.selection_warning != null and std.mem.eql(u8, selection_result.selection_warning.?, warning_tokens.selection_portal)) {
        runtime.selectPortalFallback();
    }

    if (parsed.dry_run) {
        if (mode == c.SHAULA_CAPTURE_MODE_AREA) {
            try json.writeAreaDryRunJson(allocator, io, selected);
        } else {
            try json.writeSelectionDryRunJson(allocator, io, command, selected);
        }
        return 0;
    }

    if (region_capture_mode == c.SHAULA_REGION_CAPTURE_MODE_FROZEN and selection_result.frozen_source == null and selection_result.selection_warning == null) {
        try json.writeErrorJson(
            io,
            command,
            "ERR_CAPTURE_BACKEND_UNAVAILABLE",
            "frozen capture source unavailable",
            true,
            reported_mode,
            null,
            false,
            &.{warning_tokens.frozen_source_missing},
        );
        return recovery_policy.exitCodeFor("ERR_CAPTURE_BACKEND_UNAVAILABLE");
    }

    const geometry = capture_types.areaGeometryFromSelection(selected.geometry);
    const options = switch (mode) {
        c.SHAULA_CAPTURE_MODE_QUICK => invocation.quick(parsed, region_capture_mode, geometry),
        c.SHAULA_CAPTURE_MODE_AREA => invocation.area(parsed, region_capture_mode, geometry),
        c.SHAULA_CAPTURE_MODE_ALL_IN_ONE => invocation.allInOne(parsed, region_capture_mode, geometry),
        else => unreachable,
    };
    var resolved_options = options;
    applyOverlayConfirmAction(&resolved_options.post_flags, selection_result.confirm_action);
    return executeLifecycle(runtime, allocator, io, environ, resolved_options, &session_lock, selection_result.frozen_source, selection_result.selection_warning);
}

fn runDirectMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    runtime: runtime_capabilities.RuntimeDecision,
    comptime mode: c.ShaulaCaptureMode,
    parsed: anytype,
) !u8 {
    const reported_mode = captureModeToken(mode);
    const command = commandForMode(mode);

    const capture_session = try beginJsonCaptureSession(allocator, io, environ, command, reported_mode, parsed.json_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    const options = switch (mode) {
        c.SHAULA_CAPTURE_MODE_FULLSCREEN => invocation.fullscreen(parsed),
        c.SHAULA_CAPTURE_MODE_ALL_SCREENS => invocation.allScreens(parsed),
        c.SHAULA_CAPTURE_MODE_FOCUSED => invocation.focused(parsed),
        c.SHAULA_CAPTURE_MODE_WINDOW => invocation.window(parsed),
        else => unreachable,
    };
    return executeLifecycle(runtime, allocator, io, environ, options, &session_lock, null, null);
}

/// Execute `capture previous-area` without fabricating missing geometry.
pub fn runPreviousArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, runtime: runtime_capabilities.RuntimeDecision, argv: []const [*:0]const u8) !u8 {
    const reported_mode = captureModeToken(c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA);
    const backend_mode = captureModeBackendToken(c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA) orelse reported_mode;
    const parsed = flags.parsePreviousAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (try validateJsonMode(io, "capture previous-area", reported_mode, parsed.json_mode)) |code| return code;

    const unsupported_rc = try guards.enforceModeSupported(runtime, io, "capture previous-area", backend_mode);
    if (unsupported_rc) |code| return code;

    const backend_label = runtime.backendUsedLabel();
    if (!runtime.previousAreaSupported() or !previous_area_store.supportedForBackendLabel(backend_label)) {
        try json.writeErrorJson(
            io,
            "capture previous-area",
            "ERR_CAPTURE_MODE_UNSUPPORTED",
            "capture mode is unsupported by runtime capabilities",
            false,
            reported_mode,
            backend_label,
            false,
            &.{warning_tokens.capability_execution_mismatch_guard},
        );
        return recovery_policy.exitCodeFor("ERR_CAPTURE_MODE_UNSUPPORTED");
    }

    const geometry = (try previous_area_store.load(allocator, io, environ)) orelse {
        try json.writeErrorJson(io, "capture previous-area", "ERR_PREVIOUS_AREA_UNAVAILABLE", "previous area is unavailable", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_PREVIOUS_AREA_UNAVAILABLE");
    };

    const capture_session = try beginCaptureSession(allocator, io, environ, "capture previous-area", reported_mode);
    if (capture_session.exit_code) |code| return code;
    var session_lock = capture_session.lock.?;
    defer session_lock.deinit();

    return executeLifecycle(runtime, allocator, io, environ, invocation.previousArea(parsed, geometry), &session_lock, null, null);
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
                &.{warning_tokens.capture_session_busy},
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

fn commandForMode(comptime mode: c.ShaulaCaptureMode) []const u8 {
    return switch (mode) {
        c.SHAULA_CAPTURE_MODE_QUICK => "capture quick",
        c.SHAULA_CAPTURE_MODE_AREA => "capture area",
        c.SHAULA_CAPTURE_MODE_FULLSCREEN => "capture fullscreen",
        c.SHAULA_CAPTURE_MODE_ALL_SCREENS => "capture all-screens",
        c.SHAULA_CAPTURE_MODE_FOCUSED => "capture focused",
        c.SHAULA_CAPTURE_MODE_WINDOW => "capture window",
        c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA => "capture previous-area",
        c.SHAULA_CAPTURE_MODE_ALL_IN_ONE => "capture all-in-one",
        else => unreachable,
    };
}

fn overlaySpecForMode(comptime mode: c.ShaulaCaptureMode) OverlayModeSpec {
    return switch (mode) {
        c.SHAULA_CAPTURE_MODE_QUICK => .{ .interaction_mode = .quick, .draft_mode = .quick },
        c.SHAULA_CAPTURE_MODE_AREA => .{ .interaction_mode = .area, .draft_mode = .area },
        c.SHAULA_CAPTURE_MODE_ALL_IN_ONE => .{ .interaction_mode = .quick, .draft_mode = .capture },
        else => unreachable,
    };
}

const OverlaySelectionOutcome = struct {
    selection: selection.SelectionResult,
    frozen_source: ?overlay_session.FrozenSource = null,
    confirm_action: overlay_session.ConfirmAction = .capture,
    selection_warning: ?[]const u8 = null,
    exit_code: ?u8 = null,

    fn deinit(self: *OverlaySelectionOutcome, allocator: std.mem.Allocator, io: std.Io) void {
        if (self.frozen_source) |source| source.deinit(allocator, io);
        self.frozen_source = null;
    }
};

fn resolveOverlaySelection(
    runtime: runtime_capabilities.RuntimeDecision,
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    interaction_mode: overlay_session.InteractionMode,
    draft_mode: overlay_draft_store.DraftMode,
    aspect: ?[]const u8,
    region_capture_mode: c.ShaulaRegionCaptureMode,
    prepared_frozen_source: *?overlay_session.PreparedFrozenSource,
    dry_run: bool,
    simulate_cancel: bool,
) !OverlaySelectionOutcome {
    const force_noninteractive_selection = env.flagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = dry_run or force_noninteractive_selection;
    var result = try overlay_session.runSelection(
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
        const confirm_action = result.confirm_action;
        result.frozen_source = null;
        return .{ .selection = result.result, .frozen_source = frozen_source, .confirm_action = confirm_action };
    }

    if (result.unavailable) {
        if (runtime.portalSelectionAvailable()) {
            return .{ .selection = result.result, .selection_warning = warning_tokens.selection_portal };
        }
    }

    const overlay_code = overlay_session.deterministicFailureCode(environ, simulate_cancel, true);
    if (overlay_code) |code| {
        const spec_pointer = c.shaula_error_taxonomy_spec_for(.{ .data = code.ptr, .length = code.len });
        if (spec_pointer == null) unreachable;
        const spec = spec_pointer[0];
        const spec_code = spec.code.data[0..spec.code.length];
        const spec_message = spec.message.data[0..spec.message.length];
        try json.writeErrorJson(io, command, spec_code, spec_message, spec.retryable != 0, reported_mode, null, false, &.{});
        return .{ .selection = result.result, .exit_code = recovery_policy.exitCodeFor(spec_code) };
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
    selection_warning: ?[]const u8,
) !u8 {
    switch (outcome.*) {
        .success => |success| {
            var warnings: [3][]const u8 = undefined;
            var warning_count: usize = 0;
            if (precondition_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (selection_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (success.degraded) {
                warnings[warning_count] = backend_contract.warning_capture_backend_degraded;
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
            var warnings: [3][]const u8 = undefined;
            var warning_count: usize = 0;
            if (precondition_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (selection_warning) |warning| {
                warnings[warning_count] = warning;
                warning_count += 1;
            }
            if (failure.degraded) {
                warnings[warning_count] = backend_contract.warning_window_capture_degraded;
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

test "overlay save action skips preview and keeps configured copy behavior" {
    var direct_save = invocation.PostCaptureFlags{
        .save = false,
        .copy = false,
        .preview = true,
    };
    applyOverlayConfirmAction(&direct_save, .save);

    try std.testing.expect(direct_save.save);
    try std.testing.expect(direct_save.save_explicit);
    try std.testing.expect(!direct_save.preview);
    try std.testing.expect(direct_save.preview_explicit);

    const with_copy = resolvePostCaptureFlags("quick", direct_save, .{
        .quick = .{ .copy_to_clipboard = true },
    });
    try std.testing.expect(with_copy.save);
    try std.testing.expect(with_copy.copy);
    try std.testing.expect(!with_copy.preview);

    const without_copy = resolvePostCaptureFlags("quick", direct_save, .{
        .quick = .{ .copy_to_clipboard = false },
    });
    try std.testing.expect(without_copy.save);
    try std.testing.expect(!without_copy.copy);
    try std.testing.expect(!without_copy.preview);
}

test "overlay copy action preserves direct copy behavior" {
    var direct_copy = invocation.PostCaptureFlags{
        .save = false,
        .copy = false,
        .preview = true,
    };
    applyOverlayConfirmAction(&direct_copy, .copy);

    try std.testing.expect(direct_copy.copy);
    try std.testing.expect(direct_copy.copy_explicit);
    try std.testing.expect(!direct_copy.preview);
    try std.testing.expect(direct_copy.preview_explicit);
}

fn resolveRegionCaptureMode(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    explicit: ?[]const u8,
) c.ShaulaRegionCaptureMode {
    if (explicit) |token| {
        return parseRegionCaptureMode(token) orelse c.SHAULA_REGION_CAPTURE_MODE_LIVE;
    }
    if (env.trimmed(environ, "SHAULA_REGION_CAPTURE_MODE")) |raw| {
        if (parseRegionCaptureMode(raw)) |mode| return mode;
    }

    var loaded = config_loader.load(allocator, io, environ) catch return c.SHAULA_REGION_CAPTURE_MODE_LIVE;
    defer loaded.deinit(allocator);
    return loaded.config.capture.region_capture_mode;
}

/// Gives Wayland/Niri one redraw opportunity after the live overlay exits.
fn settleAfterLiveOverlay(
    io: std.Io,
    environ: std.process.Environ,
    region_capture_mode: c.ShaulaRegionCaptureMode,
) void {
    if (region_capture_mode != c.SHAULA_REGION_CAPTURE_MODE_LIVE) return;

    const settle_ms = env.unsignedOrDefault(u64, environ, "SHAULA_LIVE_REGION_SETTLE_MS", 50);
    if (settle_ms == 0) return;
    const millis_i64: i64 = @intCast(settle_ms);
    const duration: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(millis_i64), .clock = .real };
    duration.sleep(io) catch {};
}
