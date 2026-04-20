const std = @import("std");

const overlay = @import("../overlay/overlay.zig");
const selection = @import("../selection/selection.zig");
const capture_types = @import("types.zig");
const capture_backend = @import("../backends/capture_backend.zig");
const post_capture_pipeline = @import("../pipeline/post_capture.zig");
const recovery_policy = @import("../recovery/policy.zig");

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
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "usage: shaula capture <area|fullscreen|window> --json", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    if (std.mem.eql(u8, subcommand, "area")) {
        return runArea(allocator, io, environ, argv);
    }
    if (std.mem.eql(u8, subcommand, "fullscreen")) {
        return runFullscreen(allocator, io, environ, argv);
    }
    if (std.mem.eql(u8, subcommand, "window")) {
        return runWindow(allocator, io, environ, argv);
    }

    try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "unsupported capture subcommand", false, null, null, false, &.{});
    return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
}

/// Execute `capture area`.
///
/// Flow summary:
/// 1. Parse/validate flags.
/// 2. Run overlay selection (or deterministic dry-run path).
/// 3. Enforce capabilities + pre-capture guard.
/// 4. Execute backend and emit stable JSON contract.
fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--json is required", false, "area", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const force_noninteractive_selection = envFlagEnabled(environ, "SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION");
    const use_overlay_dry_run = parsed.dry_run or force_noninteractive_selection or !guards.hasInteractiveOverlayBinary(io);
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
        try json.writeErrorJson(io, "capture area", "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, "area", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED");
    }

    if (parsed.dry_run) {
        try json.writeAreaDryRunJson(allocator, io, selection_result);
        return 0;
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture area", "area");
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture area", "area") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .area,
        .output_path = parsed.output,
        .area_geometry = if (selection_result.geometry) |g|
            .{ .x = g.x, .y = g.y, .width = g.width, .height = g.height }
        else
            null,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture area", &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture fullscreen` with strict capability/guard enforcement.
fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--json is required", false, "fullscreen", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture fullscreen", "fullscreen");
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture fullscreen", "fullscreen") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .fullscreen,
        .output_path = parsed.output,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture fullscreen", &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

/// Execute `capture window`.
///
/// Window mode is currently capability-gated off for Niri v1 scope; this path is
/// kept explicit to preserve deterministic contracts and future extension points.
fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const parsed = flags.parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!parsed.json_mode) {
        try json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--json is required", false, "window", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try guards.enforceModeSupported(io, environ, "capture window", "window");
    if (unsupported_rc) |code| return code;

    const precondition_warning = guards.enforcePreCaptureGuard(allocator, io, environ, "capture window", "window") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .window,
        .output_path = parsed.output,
        .window_id = parsed.window_id,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture window", &outcome, .{
        .save = parsed.save,
        .copy = parsed.copy,
    }, precondition_warning);
}

fn writeCaptureOutcome(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    outcome: *capture_types.CaptureOutcome,
    post_flags: PostCaptureFlags,
    precondition_warning: ?[]const u8,
) !u8 {
    switch (outcome.*) {
        .success => |success| {
            if (post_flags.save or post_flags.copy) {
                try post_capture_pipeline.writeCapturePipelineJson(allocator, io, environ, command, success, .{
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

            try json.writeSuccessJson(allocator, io, command, success, warnings[0..warning_count]);
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
                capture_types.modeString(failure.mode),
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
