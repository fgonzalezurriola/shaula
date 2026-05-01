const std = @import("std");
const protocol = @import("../ipc/protocol.zig");
const json_helpers = @import("cli_json.zig");

pub fn runDaemonStart(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const already_running = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (already_running) {
        try json_helpers.writeErrorJson(io, "daemon start", "ERR_DAEMON_ALREADY_RUNNING", "daemon already running", false);
        return error.DaemonAlreadyRunning;
    }

    const exe = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(exe);

    var child = std.process.spawn(io, .{
        .argv = &.{ exe, "daemon", "_serve", "--json", "--socket", socket_path },
        .stdin = .ignore,
        .stdout = .ignore,
        .stderr = .ignore,
    }) catch {
        try json_helpers.writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "failed to spawn resident daemon", false);
        return error.IpcBindFailed;
    };

    defer if (child.id != null) child.kill(io);

    var attempts: usize = 0;
    while (attempts < 40) : (attempts += 1) {
        const socket_ready = blk: {
            std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
            break :blk true;
        };
        if (socket_ready) {
            const ts = try json_helpers.nowIso8601(allocator, io);
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
                    try json_helpers.writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "daemon failed to bind IPC socket", false);
                    return error.IpcBindFailed;
                }
            };
        }

        if (child.id == null) {
            try json_helpers.writeErrorJson(io, "daemon start", "ERR_IPC_BIND_FAILED", "daemon failed to bind IPC socket", false);
            return error.IpcBindFailed;
        }

        const delay: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(50), .clock = .real };
        delay.sleep(io) catch {};
    }

    try json_helpers.writeErrorJson(io, "daemon start", "ERR_IPC_TIMEOUT", "daemon did not reach ready state before timeout", true);
    return error.IpcTimeout;
}

pub fn runDaemonStatus(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const exists = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (!exists) {
        try json_helpers.writeErrorJson(io, "daemon status", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
        return error.DaemonNotRunning;
    }

    const status_snapshot = sendDaemonStatusAndRead(allocator, io, socket_path) catch |err| switch (err) {
        error.NotRunning => {
            try json_helpers.writeErrorJson(io, "daemon status", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
            return error.DaemonNotRunning;
        },
        error.Timeout, error.InvalidResponse => {
            try json_helpers.writeErrorJson(io, "daemon status", "ERR_IPC_TIMEOUT", "daemon status request timed out", true);
            return error.IpcTimeout;
        },
        else => return err,
    };
    defer allocator.free(status_snapshot.ipc_version);

    const ts = try json_helpers.nowIso8601(allocator, io);
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

pub fn runDaemonStop(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) !u8 {
    const exists = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };
    if (!exists) {
        try json_helpers.writeErrorJson(io, "daemon stop", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
        return error.DaemonNotRunning;
    }

    const stopped = sendDaemonStopAndWait(io, socket_path) catch |err| switch (err) {
        error.NotRunning => {
            try json_helpers.writeErrorJson(io, "daemon stop", "ERR_DAEMON_NOT_RUNNING", "daemon is not running", false);
            return error.DaemonNotRunning;
        },
        error.Timeout => {
            try json_helpers.writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
            return error.IpcTimeout;
        },
        else => {
            try json_helpers.writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
            return error.IpcTimeout;
        },
    };

    if (!stopped) {
        try json_helpers.writeErrorJson(io, "daemon stop", "ERR_IPC_TIMEOUT", "daemon did not terminate before timeout", true);
        return error.IpcTimeout;
    }

    const ts = try json_helpers.nowIso8601(allocator, io);
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

pub const DaemonStatusSnapshot = struct {
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

pub fn daemonCommand(subcommand: []const u8) []const u8 {
    if (std.mem.eql(u8, subcommand, "start")) return "daemon start";
    if (std.mem.eql(u8, subcommand, "status")) return "daemon status";
    if (std.mem.eql(u8, subcommand, "stop")) return "daemon stop";
    if (std.mem.eql(u8, subcommand, "_serve")) return "daemon _serve";
    return "daemon";
}
