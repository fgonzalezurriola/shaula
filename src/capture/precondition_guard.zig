const std = @import("std");

pub const GuardOutcome = struct {
    warning: ?[]const u8,
};

pub const GuardResult = union(enum) {
    ok: GuardOutcome,
    timeout,
};

const GuardConfig = struct {
    timeout_ms: u64,
    handshake_timeout_ms: u64,
    settle_barrier_ms: u64,
    poll_interval_ms: u64,
    require_handshake: bool,
};

pub fn enforce(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !GuardResult {
    _ = allocator;

    const config = loadConfig(environ);
    const start_ms = nowMs(io);
    const deadline_ms = start_ms +| config.timeout_ms;

    if (hasImmediatePanelHiddenHandshake(environ)) {
        return .{ .ok = .{ .warning = "capture_precondition_panel_hidden_handshake" } };
    }

    const token_path = panelHiddenTokenPath(environ);
    if (token_path) |path| {
        const handshake_wait_deadline = start_ms +| @min(config.handshake_timeout_ms, config.timeout_ms);
        if (waitForPanelHiddenToken(io, path, handshake_wait_deadline, deadline_ms, config.poll_interval_ms)) {
            return .{ .ok = .{ .warning = "capture_precondition_panel_hidden_handshake" } };
        }

        if (config.require_handshake) {
            return .timeout;
        }
    } else if (config.require_handshake) {
        if (!waitUntil(io, deadline_ms, deadline_ms, config.poll_interval_ms)) {
            return .timeout;
        }
        return .timeout;
    }

    if (config.settle_barrier_ms == 0) {
        return .{ .ok = .{ .warning = null } };
    }

    const settle_target_ms = nowMs(io) +| config.settle_barrier_ms;
    if (!waitUntil(io, settle_target_ms, deadline_ms, config.poll_interval_ms)) {
        return .timeout;
    }

    return .{ .ok = .{ .warning = "capture_precondition_settle_barrier" } };
}

fn loadConfig(environ: std.process.Environ) GuardConfig {
    const timeout_ms = parseEnvMs(environ, "SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS", 120, 2000);
    const handshake_timeout_raw = parseEnvMs(environ, "SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS", 60, 2000);
    const settle_barrier_raw = parseEnvMs(environ, "SHAULA_CAPTURE_SETTLE_BARRIER_MS", 34, 1000);

    return .{
        .timeout_ms = timeout_ms,
        .handshake_timeout_ms = @min(handshake_timeout_raw, timeout_ms),
        .settle_barrier_ms = @min(settle_barrier_raw, timeout_ms),
        .poll_interval_ms = 5,
        .require_handshake = parseEnvBool(environ, "SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE", false),
    };
}

fn parseEnvMs(environ: std.process.Environ, key: []const u8, default_value: u64, max_value: u64) u64 {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len == 0) return default_value;

        const parsed = std.fmt.parseInt(u64, raw, 10) catch return default_value;
        if (parsed == 0) return 0;
        return @min(parsed, max_value);
    }

    return default_value;
}

fn parseEnvBool(environ: std.process.Environ, key: []const u8, default_value: bool) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len == 0) return default_value;

        if (std.ascii.eqlIgnoreCase(raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes") or std.ascii.eqlIgnoreCase(raw, "on")) {
            return true;
        }
        if (std.ascii.eqlIgnoreCase(raw, "0") or std.ascii.eqlIgnoreCase(raw, "false") or std.ascii.eqlIgnoreCase(raw, "no") or std.ascii.eqlIgnoreCase(raw, "off")) {
            return false;
        }
    }

    return default_value;
}

fn hasImmediatePanelHiddenHandshake(environ: std.process.Environ) bool {
    if (parseEnvBool(environ, "SHAULA_PANEL_HIDDEN", false)) return true;

    if (environ.getPosix("SHAULA_PANEL_STATE")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (std.ascii.eqlIgnoreCase(raw, "hidden")) return true;
    }

    return false;
}

fn panelHiddenTokenPath(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_PANEL_HIDDEN_TOKEN_FILE")) |token_z| {
        const token = std.mem.trim(u8, std.mem.sliceTo(token_z, 0), " \t\r\n");
        if (token.len > 0) return token;
    }
    return null;
}

fn waitForPanelHiddenToken(
    io: std.Io,
    token_path: []const u8,
    handshake_deadline_ms: u64,
    timeout_deadline_ms: u64,
    poll_interval_ms: u64,
) bool {
    while (true) {
        if (panelHiddenTokenObserved(io, token_path)) {
            return true;
        }

        const now_ms = nowMs(io);
        if (now_ms >= handshake_deadline_ms) {
            return false;
        }
        if (now_ms >= timeout_deadline_ms) {
            return false;
        }

        const until_handshake = handshake_deadline_ms - now_ms;
        const until_timeout = timeout_deadline_ms - now_ms;
        const sleep_ms = minU64(minU64(until_handshake, until_timeout), poll_interval_ms);
        if (sleep_ms == 0) {
            return false;
        }
        sleepForMs(io, sleep_ms);
    }
}

fn panelHiddenTokenObserved(io: std.Io, token_path: []const u8) bool {
    std.Io.Dir.accessAbsolute(io, token_path, .{}) catch return false;
    return true;
}

fn waitUntil(io: std.Io, target_ms: u64, deadline_ms: u64, poll_interval_ms: u64) bool {
    while (true) {
        const now_ms = nowMs(io);
        if (now_ms >= target_ms) return true;
        if (now_ms >= deadline_ms) return false;

        const until_target = target_ms - now_ms;
        const until_deadline = deadline_ms - now_ms;
        const sleep_ms = minU64(minU64(until_target, until_deadline), poll_interval_ms);
        if (sleep_ms == 0) return false;
        sleepForMs(io, sleep_ms);
    }
}

fn sleepForMs(io: std.Io, ms: u64) void {
    const millis_i64: i64 = @intCast(ms);
    const duration: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(millis_i64), .clock = .real };
    duration.sleep(io) catch {};
}

fn nowMs(io: std.Io) u64 {
    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    if (millis <= 0) return 0;
    return @intCast(millis);
}

fn minU64(a: u64, b: u64) u64 {
    return if (a < b) a else b;
}

const EnvPair = struct {
    key: []const u8,
    value: []const u8,
};

const TestEnviron = struct {
    environ: std.process.Environ,
    block: std.process.Environ.Block,

    fn deinit(self: *TestEnviron, allocator: std.mem.Allocator) void {
        self.block.deinit(allocator);
    }
};

fn initTestEnviron(allocator: std.mem.Allocator, pairs: []const EnvPair) !TestEnviron {
    var map = std.process.Environ.Map.init(allocator);
    defer map.deinit();

    for (pairs) |pair| {
        try map.put(pair.key, pair.value);
    }

    const block = try map.createPosixBlock(allocator, .{});
    return .{
        .environ = .{ .block = block },
        .block = block,
    };
}

test "precondition guard accepts immediate panel hidden handshake" {
    var test_env = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_PANEL_HIDDEN", .value = "1" },
    });
    defer test_env.deinit(std.testing.allocator);

    const result = try enforce(std.testing.allocator, std.testing.io, test_env.environ);
    switch (result) {
        .ok => |ok| {
            try std.testing.expect(ok.warning != null);
            try std.testing.expectEqualStrings("capture_precondition_panel_hidden_handshake", ok.warning.?);
        },
        .timeout => return error.TestExpectedSuccess,
    }
}

test "precondition guard falls back to settle barrier when handshake unavailable" {
    var test_env = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS", .value = "120" },
        .{ .key = "SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS", .value = "25" },
        .{ .key = "SHAULA_CAPTURE_SETTLE_BARRIER_MS", .value = "1" },
    });
    defer test_env.deinit(std.testing.allocator);

    const result = try enforce(std.testing.allocator, std.testing.io, test_env.environ);
    switch (result) {
        .ok => |ok| {
            try std.testing.expect(ok.warning != null);
            try std.testing.expectEqualStrings("capture_precondition_settle_barrier", ok.warning.?);
        },
        .timeout => return error.TestExpectedSuccess,
    }
}

test "precondition guard returns timeout when required handshake is unavailable" {
    var test_env = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_CAPTURE_PRECONDITION_TIMEOUT_MS", .value = "20" },
        .{ .key = "SHAULA_CAPTURE_REQUIRE_PANEL_HIDDEN_HANDSHAKE", .value = "1" },
        .{ .key = "SHAULA_PANEL_HANDSHAKE_TIMEOUT_MS", .value = "10" },
        .{ .key = "SHAULA_PANEL_HIDDEN_TOKEN_FILE", .value = "/tmp/shaula/nonexistent-required-handshake.token" },
        .{ .key = "SHAULA_CAPTURE_SETTLE_BARRIER_MS", .value = "5" },
    });
    defer test_env.deinit(std.testing.allocator);

    const result = try enforce(std.testing.allocator, std.testing.io, test_env.environ);
    switch (result) {
        .ok => return error.TestExpectedTimeout,
        .timeout => {},
    }
}
