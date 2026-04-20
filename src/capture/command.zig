const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const overlay = @import("../overlay/overlay.zig");
const selection = @import("../selection/selection.zig");
const precondition_guard = @import("precondition_guard.zig");
const capture_types = @import("types.zig");
const capture_backend = @import("../backends/capture_backend.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const post_capture_pipeline = @import("../pipeline/post_capture.zig");
const recovery_policy = @import("../recovery/policy.zig");

const AreaFlags = struct {
    json_mode: bool = false,
    dry_run: bool = false,
    simulate_cancel: bool = false,
    save: bool = false,
    copy: bool = false,
    aspect: ?[]const u8 = null,
    output: ?[]const u8 = null,
};

const FullscreenFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    output: ?[]const u8 = null,
};

const WindowFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    output: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
};

pub fn run(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    argv: []const [*:0]const u8,
) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "capture", "ERR_CLI_USAGE", "usage: shaula capture <area|fullscreen|window> --json", false, null, null, false, &.{});
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

    try writeErrorJson(io, "capture", "ERR_CLI_USAGE", "unsupported capture subcommand", false, null, null, false, &.{});
    return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
}

fn runArea(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const flags = parseAreaFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!flags.json_mode) {
        try writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--json is required", false, "area", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const use_overlay_dry_run = flags.dry_run or !hasInteractiveOverlayBinary(io);
    const selection_result = try overlay.runSelection(
        allocator,
        io,
        environ,
        selection.SelectionMode.freeform,
        .{ .aspect = flags.aspect },
        use_overlay_dry_run,
        flags.simulate_cancel,
    );
    if (selection_result.cancelled) {
        try writeErrorJson(io, "capture area", "ERR_SELECTION_CANCELLED", "selection was cancelled by user", false, "area", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_SELECTION_CANCELLED");
    }

    if (flags.dry_run) {
        try writeAreaDryRunJson(allocator, io, selection_result);
        return 0;
    }

    const unsupported_rc = try enforceModeSupported(io, environ, "capture area", "area");
    if (unsupported_rc) |code| return code;

    const precondition_warning = enforcePreCaptureGuard(allocator, io, environ, "capture area", "area") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .area,
        .output_path = flags.output,
        .area_geometry = if (selection_result.geometry) |g|
            .{ .x = g.x, .y = g.y, .width = g.width, .height = g.height }
        else
            null,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture area", &outcome, .{
        .save = flags.save,
        .copy = flags.copy,
    }, precondition_warning);
}

fn runFullscreen(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const flags = parseFullscreenFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!flags.json_mode) {
        try writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--json is required", false, "fullscreen", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try enforceModeSupported(io, environ, "capture fullscreen", "fullscreen");
    if (unsupported_rc) |code| return code;

    const precondition_warning = enforcePreCaptureGuard(allocator, io, environ, "capture fullscreen", "fullscreen") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .fullscreen,
        .output_path = flags.output,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture fullscreen", &outcome, .{
        .save = flags.save,
        .copy = flags.copy,
    }, precondition_warning);
}

fn runWindow(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const flags = parseWindowFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!flags.json_mode) {
        try writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--json is required", false, "window", null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const unsupported_rc = try enforceModeSupported(io, environ, "capture window", "window");
    if (unsupported_rc) |code| return code;

    const precondition_warning = enforcePreCaptureGuard(allocator, io, environ, "capture window", "window") catch |err| switch (err) {
        error.PreconditionTimeout => return recovery_policy.exitCodeFor("ERR_CAPTURE_PRECONDITION_TIMEOUT"),
        else => return err,
    };

    var outcome = try capture_backend.execute(allocator, io, environ, .{
        .mode = .window,
        .output_path = flags.output,
        .window_id = flags.window_id,
    });
    defer capture_backend.deinitOutcome(allocator, &outcome);
    return writeCaptureOutcome(allocator, io, environ, "capture window", &outcome, .{
        .save = flags.save,
        .copy = flags.copy,
    }, precondition_warning);
}

fn enforceModeSupported(io: std.Io, environ: std.process.Environ, command: []const u8, mode: []const u8) !?u8 {
    const runtime = runtime_capabilities.resolve(environ);
    if (runtime.backend == .stub) {
        const backend_used = runtime_capabilities.backendLabel(runtime.backend);
        try writeErrorJson(
            io,
            command,
            "ERR_CAPTURE_BACKEND_UNAVAILABLE",
            "capture backend unavailable",
            true,
            mode,
            backend_used,
            false,
            &.{},
        );

        return recovery_policy.exitCodeFor("ERR_CAPTURE_BACKEND_UNAVAILABLE");
    }

    if (runtime_capabilities.modeSupported(runtime.capture, mode)) {
        return null;
    }

    const backend_used = runtime_capabilities.backendLabel(runtime.backend);
    try writeErrorJson(
        io,
        command,
        "ERR_CAPTURE_MODE_UNSUPPORTED",
        "capture mode is unsupported by runtime capabilities",
        false,
        mode,
        backend_used,
        modeDegraded(mode),
        &.{"capability_execution_mismatch_guard"},
    );

    return recovery_policy.exitCodeFor("ERR_CAPTURE_MODE_UNSUPPORTED");
}

fn hasInteractiveOverlayBinary(io: std.Io) bool {
    const candidates = [_][]const u8{
        "/usr/bin/slurp",
        "/bin/slurp",
        "/usr/local/bin/slurp",
    };

    for (candidates) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return true;
    }

    return false;
}

fn enforcePreCaptureGuard(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    mode: []const u8,
) !?[]const u8 {
    const guard_result = try precondition_guard.enforce(allocator, io, environ);
    switch (guard_result) {
        .ok => |ok| return ok.warning,
        .timeout => {
            const runtime = runtime_capabilities.resolve(environ);
            const backend_used = runtime_capabilities.backendLabel(runtime.backend);

            try writeErrorJson(
                io,
                command,
                "ERR_CAPTURE_PRECONDITION_TIMEOUT",
                "capture precondition timed out waiting for shell artifact guard",
                true,
                mode,
                backend_used,
                modeDegraded(mode),
                &.{"capture_precondition_guard_timeout"},
            );

            return error.PreconditionTimeout;
        },
    }
}

fn modeDegraded(mode: []const u8) bool {
    return std.mem.eql(u8, mode, "window");
}

fn parseAreaFlags(io: std.Io, argv: []const [*:0]const u8) !AreaFlags {
    var flags: AreaFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--dry-run")) {
            flags.dry_run = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--simulate-cancel")) {
            flags.simulate_cancel = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--aspect")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--aspect requires a value", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.aspect = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--output requires a path", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
            continue;
        }

        try writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "unsupported flag", false, "area", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

fn parseFullscreenFlags(io: std.Io, argv: []const [*:0]const u8) !FullscreenFlags {
    var flags: FullscreenFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--output requires a path", false, "fullscreen", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
            continue;
        }

        try writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "unsupported flag", false, "fullscreen", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

fn parseWindowFlags(io: std.Io, argv: []const [*:0]const u8) !WindowFlags {
    var flags: WindowFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--output requires a path", false, "window", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--window-id")) {
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--window-id requires a value", false, "window", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.window_id = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
            continue;
        }

        try writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "unsupported flag", false, "window", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

const PostCaptureFlags = struct {
    save: bool,
    copy: bool,
};

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

            try writeSuccessJson(allocator, io, command, success, warnings[0..warning_count]);
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

            try writeErrorJson(
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

fn writeSuccessJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    success: capture_types.CaptureSuccess,
    warnings: []const []const u8,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    const mode = capture_types.modeString(success.mode);
    const mode_json = try jsonStringAlloc(allocator, mode);
    defer allocator.free(mode_json);
    const path_json = try jsonStringAlloc(allocator, success.path);
    defer allocator.free(path_json);
    const mime_json = try jsonStringAlloc(allocator, success.mime);
    defer allocator.free(mime_json);
    const backend_json = try jsonStringAlloc(allocator, success.backend_used);
    defer allocator.free(backend_json);

    const warnings_json = try warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"degraded\":{s},\"result\":{{\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            if (success.degraded) "true" else "false",
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

fn writeAreaDryRunJson(allocator: std.mem.Allocator, io: std.Io, result: selection.SelectionResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);

    const mode_json = try jsonStringAlloc(allocator, @tagName(result.mode));
    defer allocator.free(mode_json);

    const aspect_json = try jsonNullableStringAlloc(allocator, result.aspect);
    defer allocator.free(aspect_json);

    const geometry_json = if (result.geometry) |g| blk: {
        break :blk try std.fmt.allocPrint(allocator, "{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}}", .{ g.x, g.y, g.width, g.height });
    } else try allocator.dupe(u8, "null");
    defer allocator.free(geometry_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"capture area\",\"timestamp\":{s},\"selection\":{{\"mode\":{s},\"aspect\":{s},\"geometry\":{s},\"cancelled\":false}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts_json, mode_json, aspect_json, geometry_json },
    );
    try stdout.interface.flush();
}

fn writeErrorJson(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    mode: ?[]const u8,
    backend_used: ?[]const u8,
    degraded: bool,
    warnings: []const []const u8,
) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const code_json = try jsonStringAlloc(allocator, code);
    defer allocator.free(code_json);
    const message_json = try jsonStringAlloc(allocator, message);
    defer allocator.free(message_json);

    const mode_json = try jsonNullableStringAlloc(allocator, mode);
    defer allocator.free(mode_json);

    const backend_json = try jsonNullableStringAlloc(allocator, backend_used);
    defer allocator.free(backend_json);

    const warning_json = try warningsJson(allocator, warnings);
    defer allocator.free(warning_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"backend_used\":{s},\"degraded\":{s},\"error\":{{\"code\":{s},\"message\":{s},\"retryable\":{s},\"details\":{{\"mode\":{s}}}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            mode_json,
            backend_json,
            if (degraded) "true" else "false",
            code_json,
            message_json,
            if (retryable) "true" else "false",
            mode_json,
            warning_json,
        },
    );
    try stdout.interface.flush();
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return jsonStringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    if (warnings.len == 0) return allocator.dupe(u8, "[]");

    var list = try std.ArrayList(u8).initCapacity(allocator, 32);
    defer list.deinit(allocator);

    try list.append(allocator, '[');
    for (warnings, 0..) |warning, index| {
        if (index != 0) {
            try list.append(allocator, ',');
        }
        try list.print(allocator, "\"{s}\"", .{warning});
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    const ts = std.Io.Timestamp.now(io, .real);
    const epoch_seconds: i64 = ts.toSeconds();

    const days: i64 = @divFloor(epoch_seconds, 86400);
    const secs_of_day: i64 = @mod(epoch_seconds, 86400);

    const z = days + 719468;
    const era = @divFloor(if (z >= 0) z else z - 146096, 146097);
    const doe = z - era * 146097;
    const yoe = @divFloor(doe - @divFloor(doe, 1460) + @divFloor(doe, 36524) - @divFloor(doe, 146096), 365);
    var y = yoe + era * 400;
    const doy = doe - (365 * yoe + @divFloor(yoe, 4) - @divFloor(yoe, 100));
    const mp = @divFloor(5 * doy + 2, 153);
    const d = doy - @divFloor(153 * mp + 2, 5) + 1;
    var m: i64 = mp + (if (mp < 10) @as(i64, 3) else @as(i64, -9));
    y += if (m <= 2) 1 else 0;

    const hh = @divFloor(secs_of_day, 3600);
    const mm = @divFloor(@mod(secs_of_day, 3600), 60);
    const ss = @mod(secs_of_day, 60);

    if (m <= 0) m += 12;

    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(y)),
        @as(u64, @intCast(m)),
        @as(u64, @intCast(d)),
        @as(u64, @intCast(hh)),
        @as(u64, @intCast(mm)),
        @as(u64, @intCast(ss)),
    });
}

test "json string helper escapes embedded quotes" {
    const encoded = try jsonStringAlloc(std.testing.allocator, "/tmp/shaula/q\"uote\".png");
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("\"/tmp/shaula/q\\\"uote\\\".png\"", encoded);
}
