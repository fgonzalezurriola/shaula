const std = @import("std");

const core_capture_mode = @import("../core/capture_mode.zig");
const overlay = @import("../overlay/overlay.zig");
const selection = @import("../selection/selection.zig");
const capture_types = @import("types.zig");
const capture_backend = @import("../backends/capture_backend.zig");
const post_capture_pipeline = @import("../pipeline/post_capture.zig");
const recovery_policy = @import("../recovery/policy.zig");
const previous_area_store = @import("../runtime/previous_area_store.zig");

const flags = @import("command_flags.zig");
const guards = @import("command_guards.zig");
const json = @import("command_json.zig");

const PostCaptureFlags = struct {
    save: bool,
    copy: bool,
};

/// Entry point for the `capture` command family.
///
/// Dispatch is intentionally strict and returns deterministic taxonomy errors for
/// unknown subcommands or invalid flag usage.
pub fn run(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    argv: []const [*:0]const u8,
) !u8 {
    if (argv.len < 3) {
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "usage: shaula capture <area|fullscreen|window|previous-area> --json", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    const requested_mode = core_capture_mode.parseCliToken(subcommand) orelse {
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "unsupported capture subcommand", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    };

    return switch (requested_mode) {
        .area => runArea(allocator, io, environ, argv),
        .fullscreen => runFullscreen(allocator, io, environ, argv),
        .window => runWindow(allocator, io, environ, argv),
        .previous_area => runPreviousArea(allocator, io, environ, argv),
        .all_in_one => runAllInOne(allocator, io, environ, argv),
    };
}

/// Execute `capture all-in-one`.
///
/// This first public all-in-one iteration keeps backend execution on the proven
/// area lane while using the dedicated all-in-one session state for toolbar
/// placement and persistence. Unsupported overlay/helper failures still map to
/// deterministic `ERR_*` outcomes or the explicit slurp fallback.
fn runAllInOne(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const reported_mode = core_capture_mode.cliToken(.all_in_one);
    const backend_mode = core_capture_mode.backendModeToken(.all_in_one) orelse reported_mode;
    const parsed = flags.parseAllInOneFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "--json is required", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const force_noninteractive_selection = envFlagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = parsed.dry_run or force_noninteractive_selection;
    const selection_result = try overlay.runSelection(
        allocator,
        io,
        environ,
        selection.SelectionMode.freeform,
        .{ .aspect = parsed.aspect },
        use_overlay_dry_run,
        parsed.simulate_cancel,
    );
    if (selection_result.cancelled) {
        const overlay_code = overlay.deterministicFailureCode(environ, parsed.simulate_cancel, true);
        if (overlay_code) |code| {
            const spec = recovery_policy.specFor(code);
            try json.writeErrorJson(io, "capture all-in-one", spec.code, spec.message, spec.retryable, reported_mode, null, false, &.{});
            return recovery_policy.exitCodeFor(spec.code);
        }

        try json.writeErrorJson(io, "capture all-in-one", "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, reported_mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED");
    }

    if (parsed.dry_run) {
        try json.writeSelectionDryRunJson(allocator, io, "capture all-in-one", selection_result);
        return 0;
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture all-in-one", backend_mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture all-in-one", backend_mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .area,
        .output_path = parsed.output,
        .area_geometry = capture_types.areaGeometryFromSelection(selection_result.geometry),
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    if (capture_types.areaGeometryFromSelection(selection_result.geometry)) |geometry| {
        switch (outcome) {
            .success => previous_area_store.store(allocator, io, environ, geometry) catch {},
            .failure => {},
        }
    }
    return writeCaptureOutcome(allocator, io, environ, "capture all-in-one", reported_mode, &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture area`.
///
/// Flow summary:
/// 1. Parse/validate flags.
/// 2. Run overlay selection (or deterministic dry-run path).
/// 3. Enforce capabilities + pre-capture guard.
/// 4. Execute backend and emit stable JSON contract.
fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.area);
    const parsed = flags.parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const force_noninteractive_selection = envFlagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = parsed.dry_run or force_noninteractive_selection;
    const selection_result = try overlay.runSelection(
        allocator,
        io,
        environ,
        selection.SelectionMode.freeform,
        .{ .aspect = parsed.aspect },
        use_overlay_dry_run,
        parsed.simulate_cancel,
    );
    if (selection_result.cancelled) {
        const overlay_code = overlay.deterministicFailureCode(environ, parsed.simulate_cancel, true);
        if (overlay_code) |code| {
            const spec = recovery_policy.specFor(code);
            try json.writeErrorJson(io, "capture area", spec.code, spec.message, spec.retryable, mode, null, false, &.{});
            return recovery_policy.exitCodeFor(spec.code);
        }

        try json.writeErrorJson(io, "capture area", "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED");
    }

    if (parsed.dry_run) {
        try json.writeAreaDryRunJson(allocator, io, selection_result);
        return 0;
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture area", mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture area", mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .area,
        .output_path = parsed.output,
        .area_geometry = capture_types.areaGeometryFromSelection(selection_result.geometry),
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    if (capture_types.areaGeometryFromSelection(selection_result.geometry)) |geometry| {
        switch (outcome) {
            .success => previous_area_store.store(allocator, io, environ, geometry) catch {},
            .failure => {},
        }
    }
    return writeCaptureOutcome(allocator, io, environ, "capture area", mode, &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture fullscreen` with strict capability/guard enforcement.
fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.fullscreen);
    const parsed = flags.parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture fullscreen", mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture fullscreen", mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .fullscreen,
        .output_path = parsed.output,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture fullscreen", mode, &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture window`.
///
/// Window mode remains explicit so capability gating and deterministic contracts
/// stay stable even when runtime support is unavailable.
fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const mode = core_capture_mode.cliToken(.window);
    const parsed = flags.parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--json is required", false, mode, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture window", mode);
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture window", mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .window,
        .output_path = parsed.output,
        .window_id = parsed.window_id,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture window", mode, &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture previous-area`.
///
/// Contract constraints:
/// - this flow never fabricates geometry; missing or malformed runtime state
///   maps deterministically to `ERR_PREVIOUS_AREA_UNAVAILABLE`.
/// - backend execution still uses the short area-capture hot path once geometry
///   is recovered.
fn runPreviousArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
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

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture previous-area", backend_mode) catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    switch (outcome) {
        .success => previous_area_store.store(allocator, io, environ, geometry) catch {},
        .failure => {},
    }
    return writeCaptureOutcome(allocator, io, environ, "capture previous-area", reported_mode, &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

fn writeCaptureOutcome(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    outcome: *capture_types.CaptureOutcome,
    post_flags: PostCaptureFlags,
    precondition_warning: ?[]const u8,
) !u8 {
    switch (outcome.*) {
        .success => |success| {
            if (post_flags.save or post_flags.copy) {
                try post_capture_pipeline.writeCapturePipelineJson(allocator, io, environ, command, reported_mode, success, .{
                    .save = post_flags.save,
                    .copy = post_flags.copy,
                });
                return 0;
            }

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

            try json.writeSuccessJson(allocator, io, command, reported_mode, success, warnings[0..warning_count]);
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

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn envFlagEnabled(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
    }
    return false;
}
