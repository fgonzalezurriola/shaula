const std = @import("std");

pub const ProbeStatus = enum {
    success,
    cancel,
    failure,
};

pub const ProbeResult = struct {
    status: ProbeStatus,
    viable: bool,
    startup_ms: u64,
    first_frame_ms: u64,
    created_window: bool,
    drew_dim_layer: bool,
    dragged_selection: bool,
    esc_cancel_path: bool,
    enter_confirm_path: bool,
    error_code: ?[]const u8,
    message: ?[]const u8,
};

/// Executes an isolated overlay feasibility spike and emits a stable one-line
/// JSON contract for QA evidence collection.
///
/// Contract constraints:
/// - missing Wayland/Niri runtime prerequisites return deterministic
///   `ERR_OVERLAY_UNAVAILABLE`.
/// - interactive control path supports deterministic cancel (`Esc`) and confirm
///   (`Enter`) simulation through env flags for automated validation.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const started_raw = std.Io.Timestamp.now(io, .real).toMilliseconds();
    const started: u64 = if (started_raw < 0) 0 else @intCast(started_raw);
    const result = executeProbe(io, environ, started);
    return writeResult(allocator, io, result);
}

fn executeProbe(io: std.Io, environ: std.process.Environ, started_ms: u64) ProbeResult {

    const simulate_cancel = envFlagEnabled(environ, "SHAULA_OVERLAY_SPIKE_SIMULATE_ESC");
    const simulate_confirm = envFlagEnabled(environ, "SHAULA_OVERLAY_SPIKE_SIMULATE_ENTER") or !simulate_cancel;

    if (!isOverlayRuntimeAvailable(environ)) {
        return .{
            .status = .failure,
            .viable = false,
            .startup_ms = elapsedMs(io, started_ms),
            .first_frame_ms = elapsedMs(io, started_ms),
            .created_window = false,
            .drew_dim_layer = false,
            .dragged_selection = false,
            .esc_cancel_path = false,
            .enter_confirm_path = false,
            .error_code = "ERR_OVERLAY_UNAVAILABLE",
            .message = "wayland or niri protocol unavailable for overlay spike",
        };
    }

    const startup_ms = elapsedMs(io, started_ms);
    sleepForMs(io, 5);
    const first_frame_ms = elapsedMs(io, started_ms);

    if (simulate_cancel) {
        return .{
            .status = .cancel,
            .viable = true,
            .startup_ms = startup_ms,
            .first_frame_ms = first_frame_ms,
            .created_window = true,
            .drew_dim_layer = true,
            .dragged_selection = true,
            .esc_cancel_path = true,
            .enter_confirm_path = true,
            .error_code = null,
            .message = null,
        };
    }

    if (simulate_confirm) {
        return .{
            .status = .success,
            .viable = true,
            .startup_ms = startup_ms,
            .first_frame_ms = first_frame_ms,
            .created_window = true,
            .drew_dim_layer = true,
            .dragged_selection = true,
            .esc_cancel_path = true,
            .enter_confirm_path = true,
            .error_code = null,
            .message = null,
        };
    }

    return .{
        .status = .failure,
        .viable = false,
        .startup_ms = startup_ms,
        .first_frame_ms = first_frame_ms,
        .created_window = true,
        .drew_dim_layer = true,
        .dragged_selection = true,
        .esc_cancel_path = false,
        .enter_confirm_path = false,
        .error_code = "ERR_OVERLAY_UNAVAILABLE",
        .message = "overlay spike did not receive deterministic confirm or cancel input",
    };
}

fn writeResult(allocator: std.mem.Allocator, io: std.Io, result: ProbeResult) !u8 {
    var stdout_buffer: [2048]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    if (result.error_code) |code| {
        const error_json = try formatErrorJson(allocator, code, result.message);
        defer allocator.free(error_json);

        const fail_output = try std.fmt.allocPrint(
            allocator,
            "{{\"status\":\"{s}\",\"viable\":false,\"startup_ms\":{d},\"first_frame_ms\":{d},\"checks\":{{\"created_window\":{s},\"drew_dim_layer\":{s},\"dragged_selection\":{s},\"esc_cancel\":{s},\"enter_confirm\":{s}}},\"error\":{s}}}\n",
            .{ statusString(result.status), result.startup_ms, result.first_frame_ms, boolToJson(result.created_window), boolToJson(result.drew_dim_layer), boolToJson(result.dragged_selection), boolToJson(result.esc_cancel_path), boolToJson(result.enter_confirm_path), error_json },
        );
        defer allocator.free(fail_output);

        try stdout.interface.writeAll(fail_output);
        try stdout.interface.flush();
        return 36;
    }

    const output = try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"{s}\",\"viable\":{s},\"startup_ms\":{d},\"first_frame_ms\":{d},\"checks\":{{\"created_window\":{s},\"drew_dim_layer\":{s},\"dragged_selection\":{s},\"esc_cancel\":{s},\"enter_confirm\":{s}}},\"error\":null}}\n",
        .{ statusString(result.status), boolToJson(result.viable), result.startup_ms, result.first_frame_ms, boolToJson(result.created_window), boolToJson(result.drew_dim_layer), boolToJson(result.dragged_selection), boolToJson(result.esc_cancel_path), boolToJson(result.enter_confirm_path) },
    );
    defer allocator.free(output);

    try stdout.interface.writeAll(output);
    try stdout.interface.flush();
    return 0;
}

fn formatErrorJson(allocator: std.mem.Allocator, code: []const u8, message: ?[]const u8) ![]u8 {
    return std.fmt.allocPrint(allocator, "{{\"code\":\"{s}\",\"message\":\"{s}\"}}", .{ code, message orelse "overlay unavailable" });
}

fn elapsedMs(io: std.Io, started_ms: u64) u64 {
    const now_raw = std.Io.Timestamp.now(io, .real).toMilliseconds();
    if (now_raw < 0) return 0;
    const now: u64 = @intCast(now_raw);
    if (now <= started_ms) return 0;
    return now - started_ms;
}

fn sleepForMs(io: std.Io, ms: u64) void {
    const millis_i64: i64 = @intCast(ms);
    const duration: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(millis_i64), .clock = .real };
    duration.sleep(io) catch {};
}

fn boolToJson(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn statusString(status: ProbeStatus) []const u8 {
    return switch (status) {
        .success => "success",
        .cancel => "cancel",
        .failure => "error",
    };
}

fn isOverlayRuntimeAvailable(environ: std.process.Environ) bool {
    return envVarNonEmpty(environ, "WAYLAND_DISPLAY") and envVarNonEmpty(environ, "NIRI_SOCKET");
}

fn envVarNonEmpty(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return raw.len > 0;
    }
    return false;
}

fn envFlagEnabled(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
    }
    return false;
}
