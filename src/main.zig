const std = @import("std");

pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");
pub const protocol_module = @import("ipc/protocol.zig");
pub const recovery_policy_module = @import("recovery/policy.zig");

const protocol = @import("ipc/protocol.zig");
const daemon_server = @import("daemon/server.zig");
const preflight_probe = @import("preflight/probe.zig");
const capabilities_probe = @import("capabilities/probe.zig");
const capture_command = @import("capture/command.zig");
const history_command = @import("history/command.zig");
const clipboard_command = @import("clipboard/command.zig");
const errors_command = @import("errors/command.zig");
const recovery_policy = @import("recovery/policy.zig");

pub fn main(init: std.process.Init) !u8 {
    const io = init.io;
    const allocator = init.gpa;
    const argv = init.minimal.args.vector;

    if (argv.len < 2) {
        try writeErrorJson(io, "", "ERR_CLI_USAGE", "usage: shaula <capture|daemon|preflight|capabilities|history|clipboard|errors> ... --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const family = argToSlice(argv[1]);

    if (std.mem.eql(u8, family, "preflight")) {
        const flags = parseFlags(io, argv, 2, true, "preflight") catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try writeErrorJson(io, "preflight", "ERR_CLI_USAGE", "--json is required", false);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        }
        return preflight_probe.run(allocator, io, init.minimal.environ, flags.socket_arg);
    }

    if (std.mem.eql(u8, family, "capabilities")) {
        var flags_start: usize = 2;
        var flags_command: []const u8 = "capabilities list";

        if (argv.len >= 3) {
            const maybe_subcommand = argToSlice(argv[2]);
            if (std.mem.eql(u8, maybe_subcommand, "list")) {
                flags_start = 3;
                flags_command = "capabilities list";
            } else if (maybe_subcommand.len == 0 or maybe_subcommand[0] != '-') {
                try writeErrorJson(io, "capabilities list", "ERR_CLI_USAGE", "usage: shaula capabilities [list] --json", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
        }

        const flags = parseFlags(io, argv, flags_start, false, flags_command) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try writeErrorJson(io, flags_command, "ERR_CLI_USAGE", "--json is required", false);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        }
        return capabilities_probe.run(allocator, io, init.minimal.environ);
    }

    if (std.mem.eql(u8, family, "capture")) {
        return capture_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "history")) {
        return history_command.run(allocator, io, argv);
    }

    if (std.mem.eql(u8, family, "clipboard")) {
        return clipboard_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "errors")) {
        return errors_command.run(allocator, io, argv);
    }

    if (!std.mem.eql(u8, family, "daemon")) {
        try writeErrorJson(io, family, "ERR_CLI_USAGE", "unsupported command family", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (argv.len < 3) {
        try writeErrorJson(io, "daemon", "ERR_CLI_USAGE", "usage: shaula daemon <start|status|stop> --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const flags = parseFlags(io, argv, 3, true, daemonCommand(argToSlice(argv[2]))) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");

    if (!flags.json_mode) {
        try writeErrorJson(io, daemonCommand(argToSlice(argv[2])), "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const socket_path = try protocol.resolveSocketPath(allocator, init.minimal.environ, flags.socket_arg);
    defer allocator.free(socket_path);

    if (std.mem.eql(u8, argToSlice(argv[2]), "_serve")) {
        var server = daemon_server.DaemonServer.init(allocator, io, socket_path);
        server.run() catch |err| {
            if (err == error.IpcBindFailed or err == error.InvalidSocketPath or err == error.SocketParentCreateFailed) {
                try writeErrorJson(io, "daemon _serve", "ERR_IPC_BIND_FAILED", "failed to bind daemon IPC socket", false);
                return recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED");
            }
            try writeErrorJson(io, "daemon _serve", "ERR_UNKNOWN_UNMAPPED", "daemon server failed with unmapped error", false);
            return recovery_policy.exitCodeFor("ERR_UNKNOWN_UNMAPPED");
        };
        return 0;
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "start")) {
        return runDaemonStart(allocator, io, socket_path);
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "status")) {
        return runDaemonStatus(allocator, io, socket_path);
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "stop")) {
        return runDaemonStop(allocator, io, socket_path);
    }

    try writeErrorJson(io, "daemon", "ERR_CLI_USAGE", "unsupported daemon subcommand", false);
    return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
}

const ParsedFlags = struct {
    json_mode: bool,
    socket_arg: ?[]const u8,
};

fn parseFlags(io: std.Io, argv: []const [*:0]const u8, start: usize, allow_socket: bool, command: []const u8) !ParsedFlags {
    var parsed: ParsedFlags = .{ .json_mode = false, .socket_arg = null };

    var i: usize = start;
    while (i < argv.len) : (i += 1) {
        const current = argToSlice(argv[i]);
        if (std.mem.eql(u8, current, "--json")) {
            parsed.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, current, "--socket")) {
            if (!allow_socket) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--socket is not supported for this command", false);
                return error.InvalidArgument;
            }
            if (i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--socket requires a path", false);
                return error.InvalidArgument;
            }
            i += 1;
            parsed.socket_arg = argToSlice(argv[i]);
            continue;
        }

        try writeErrorJson(io, command, "ERR_CLI_USAGE", "unsupported flag", false);
        return error.InvalidArgument;
    }

    return parsed;
}

test "cli error usage code maps to taxonomy" {
    try std.testing.expectEqual(@as(u8, 2), recovery_policy.exitCodeFor("ERR_CLI_USAGE"));
}

fn runDaemonStart(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const already_running = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (already_running) {
        try writeErrorJson(io, "daemon start", "ERR_DAEMON_ALREADY_RUNNING", "daemon already running", false);
        return recovery_policy.exitCodeFor("ERR_DAEMON_ALREADY_RUNNING");
    }

    const exe = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(exe);

    var child = std.process.spawn(io, .{
        .argv = &.{ exe, "daemon", "_serve", "--json", "--socket", socket_path },
        .stdin = .ignore,
        .stdout = .ignore,
        .stderr = .ignore,
    }) catch {
        try writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "failed to spawn resident daemon", false);
        return recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED");
    };

    defer if (child.id != null) child.kill(io);

    var attempts: usize = 0;
    while (attempts < 40) : (attempts += 1) {
        const socket_ready = blk: {
            std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
            break :blk true;
        };
        if (socket_ready) {
            const ts = try nowIso8601(allocator, io);
            defer allocator.free(ts);

            var stdout_buffer: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
            try stdout.interface.print(
                "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"daemon start\",\"timestamp\":\"{s}\",\"status\":\"ready\",\"result\":{{\"status\":\"ready\",\"state\":\"ready\",\"socket\":\"{s}\"}},\"warnings\":[]}}\n",
                .{ protocol.contract_version, ts, socket_path },
            );
            try stdout.interface.flush();

            child.id = null;
            return 0;
        }

        if (child.id != null) {
            std.posix.kill(child.id.?, @enumFromInt(0)) catch |err| {
                if (err == error.ProcessNotFound) {
                    try writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "daemon failed to bind IPC socket", false);
                    return recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED");
                }
            };
        }

        if (child.id == null) {
            try writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "daemon failed to bind IPC socket", false);
            return recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED");
        }

        const delay: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(50), .clock = .real };
        delay.sleep(io) catch {};
    }

    try writeErrorJson(io, "daemon start", "ERR_IPC_TIMEOUT", "daemon did not reach ready state before timeout", true);
    return recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT");
}

fn runDaemonStatus(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const exists = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (!exists) {
        try writeErrorJson(io, "daemon status", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
        return recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING");
    }

    const status_snapshot = sendDaemonStatusAndRead(allocator, io, socket_path) catch |err| switch (err) {
        error.NotRunning => {
            try writeErrorJson(io, "daemon status", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
            return recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING");
        },
        error.Timeout, error.InvalidResponse => {
            try writeErrorJson(io, "daemon status", "ERR_IPC_TIMEOUT", "daemon status request timed out", true);
            return recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT");
        },
        else => return err,
    };
    defer allocator.free(status_snapshot.ipc_version);

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"daemon status\",\"timestamp\":\"{s}\",\"state\":\"{s}\",\"result\":{{\"state\":\"{s}\",\"degraded\":{s},\"ipc_version\":\"{s}\"}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            ts,
            status_snapshot.state.asString(),
            status_snapshot.state.asString(),
            if (status_snapshot.degraded) "true" else "false",
            status_snapshot.ipc_version,
        },
    );
    try stdout.interface.flush();
    return 0;
}

const DaemonStatusSnapshot = struct {
    state: protocol.LifecycleState,
    degraded: bool,
    ipc_version: []u8,
};

fn sendDaemonStatusAndRead(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !DaemonStatusSnapshot {
    const unix_address = try std.Io.net.UnixAddress.init(socket_path);
    var stream = std.Io.net.UnixAddress.connect(&unix_address, io) catch {
        return error.NotRunning;
    };
    defer stream.close(io);

    var write_buffer: [1024]u8 = undefined;
    var reader_buffer: [4096]u8 = undefined;

    var writer = stream.writer(io, &write_buffer);
    var reader = stream.reader(io, &reader_buffer);

    writer.interface.print(
        "{{\"ipc_version\":\"{s}\",\"request_id\":\"cli-status\",\"command\":\"daemon.status\"}}\n",
        .{protocol.ipc_version},
    ) catch {
        return error.NotRunning;
    };
    writer.interface.flush() catch {
        return error.NotRunning;
    };

    const response = reader.interface.takeDelimiterExclusive('\n') catch return error.Timeout;
    return parseDaemonStatusResponse(allocator, response);
}

fn parseDaemonStatusResponse(allocator: std.mem.Allocator, response: []const u8) !DaemonStatusSnapshot {
    const parsed = std.json.parseFromSlice(std.json.Value, allocator, response, .{}) catch {
        return error.InvalidResponse;
    };
    defer parsed.deinit();

    const root = switch (parsed.value) {
        .object => |root_object| root_object,
        else => return error.InvalidResponse,
    };

    const ok = if (root.get("ok")) |ok_value|
        valueAsBool(ok_value) orelse return error.InvalidResponse
    else
        return error.InvalidResponse;
    if (!ok) {
        return error.InvalidResponse;
    }

    var state_text: ?[]const u8 = null;
    if (root.get("daemon_state")) |daemon_state_value| {
        state_text = valueAsString(daemon_state_value);
    }
    if (state_text == null) {
        if (root.get("result")) |result_value| {
            switch (result_value) {
                .object => |result_object| {
                    if (result_object.get("state")) |result_state_value| {
                        state_text = valueAsString(result_state_value);
                    }
                },
                else => {},
            }
        }
    }

    const state = lifecycleStateFromString(state_text orelse return error.InvalidResponse) orelse {
        return error.InvalidResponse;
    };

    const degraded = if (root.get("degraded")) |degraded_value|
        valueAsBool(degraded_value) orelse (state == .degraded)
    else
        state == .degraded;

    const ipc_version = if (root.get("ipc_version")) |ipc_version_value| blk: {
        const raw_version = valueAsString(ipc_version_value) orelse break :blk try allocator.dupe(u8, protocol.ipc_version);
        break :blk try allocator.dupe(u8, raw_version);
    } else try allocator.dupe(u8, protocol.ipc_version);

    return .{
        .state = state,
        .degraded = degraded,
        .ipc_version = ipc_version,
    };
}

fn lifecycleStateFromString(raw_state: []const u8) ?protocol.LifecycleState {
    if (std.mem.eql(u8, raw_state, "initializing")) return .initializing;
    if (std.mem.eql(u8, raw_state, "ready")) return .ready;
    if (std.mem.eql(u8, raw_state, "capturing")) return .capturing;
    if (std.mem.eql(u8, raw_state, "degraded")) return .degraded;
    if (std.mem.eql(u8, raw_state, "error")) return .@"error";
    return null;
}

fn valueAsString(value: std.json.Value) ?[]const u8 {
    return switch (value) {
        .string => |s| s,
        else => null,
    };
}

fn valueAsBool(value: std.json.Value) ?bool {
    return switch (value) {
        .bool => |b| b,
        else => null,
    };
}

fn runDaemonStop(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const exists = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (!exists) {
        try writeErrorJson(io, "daemon stop", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
        return recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING");
    }

    const stopped = sendDaemonStopAndWait(io, socket_path) catch |err| switch (err) {
        error.NotRunning => {
            try writeErrorJson(io, "daemon stop", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
            return recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING");
        },
        error.Timeout => {
            try writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
            return recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT");
        },
        else => {
            try writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
            return recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT");
        },
    };

    if (!stopped) {
        try writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
        return recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT");
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"daemon stop\",\"timestamp\":\"{s}\",\"stopped\":{s},\"result\":{{\"stopped\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, ts, "true", "true" },
    );
    try stdout.interface.flush();
    return 0;
}

fn sendDaemonStopAndWait(io: std.Io, socket_path: []const u8) !bool {
    const unix_address = try std.Io.net.UnixAddress.init(socket_path);
    var stream = std.Io.net.UnixAddress.connect(&unix_address, io) catch {
        return error.NotRunning;
    };
    defer stream.close(io);

    var write_buffer: [1024]u8 = undefined;
    var reader_buffer: [4096]u8 = undefined;

    var writer = stream.writer(io, &write_buffer);
    var reader = stream.reader(io, &reader_buffer);

    try writer.interface.print(
        "{{\"ipc_version\":\"{s}\",\"request_id\":\"cli-stop\",\"command\":\"daemon.stop\"}}\n",
        .{protocol.ipc_version},
    );
    try writer.interface.flush();

    const response = reader.interface.takeDelimiterExclusive('\n') catch return error.Timeout;
    if (std.mem.indexOf(u8, response, "\"ok\":true") == null) {
        return error.ConnectionResetByPeer;
    }

    var attempts: usize = 0;
    while (attempts < 40) : (attempts += 1) {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch return true;
        const delay: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(50), .clock = .real };
        delay.sleep(io) catch {};
    }

    return false;
}

fn daemonCommand(subcommand: []const u8) []const u8 {
    if (std.mem.eql(u8, subcommand, "start")) return "daemon start";
    if (std.mem.eql(u8, subcommand, "status")) return "daemon status";
    if (std.mem.eql(u8, subcommand, "stop")) return "daemon stop";
    if (std.mem.eql(u8, subcommand, "_serve")) return "daemon _serve";
    return "daemon";
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":false,\"contract_version\":\"{s}\",\"command\":\"{s}\",\"timestamp\":\"{s}\",\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\",\"retryable\":{s},\"details\":{{}}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command, ts, code, message, if (retryable) "true" else "false" },
    );
    try stdout.interface.flush();
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

test "daemon command names are stable" {
    try std.testing.expectEqualStrings("daemon start", daemonCommand("start"));
    try std.testing.expectEqualStrings("daemon", daemonCommand("x"));
}
