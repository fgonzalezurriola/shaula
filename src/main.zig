const std = @import("std");

pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");
pub const protocol_module = @import("ipc/protocol.zig");
pub const recovery_policy_module = @import("recovery/policy.zig");

const protocol = @import("ipc/protocol.zig");
const daemon_server = @import("daemon/server.zig");
const daemon_cli = @import("daemon/cli_control.zig");
const daemon_json = @import("daemon/cli_json.zig");
const preflight_probe = @import("preflight/probe.zig");
const capabilities_probe = @import("capabilities/probe.zig");
const capture_command = @import("capture/command.zig");
const history_command = @import("history/command.zig");
const clipboard_command = @import("clipboard/command.zig");
const preview_command = @import("preview/command.zig");
const errors_command = @import("errors/command.zig");
const recovery_policy = @import("recovery/policy.zig");

pub fn main(init: std.process.Init) !u8 {
    const io = init.io;
    const allocator = init.gpa;
    const argv = init.minimal.args.vector;

    if (argv.len < 2) {
        try daemon_json.writeErrorJson(io, "", "ERR_CLI_USAGE", "usage: shaula <capture|preview|daemon|preflight|capabilities|history|clipboard|errors> ... --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const family = argToSlice(argv[1]);

    if (std.mem.eql(u8, family, "preflight")) {
        const flags = parseFlags(io, argv, 2, true, "preflight") catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try daemon_json.writeErrorJson(io, "preflight", "ERR_CLI_USAGE", "--json is required", false);
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
                try daemon_json.writeErrorJson(io, "capabilities list", "ERR_CLI_USAGE", "usage: shaula capabilities [list] --json", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
        }

        const flags = parseFlags(io, argv, flags_start, false, flags_command) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try daemon_json.writeErrorJson(io, flags_command, "ERR_CLI_USAGE", "--json is required", false);
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

    if (std.mem.eql(u8, family, "preview")) {
        return preview_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "errors")) {
        return errors_command.run(allocator, io, argv);
    }

    if (!std.mem.eql(u8, family, "daemon")) {
        try daemon_json.writeErrorJson(io, family, "ERR_CLI_USAGE", "unsupported command family", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (argv.len < 3) {
        try daemon_json.writeErrorJson(io, "daemon", "ERR_CLI_USAGE", "usage: shaula daemon <start|status|stop> --json", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const flags = parseFlags(io, argv, 3, true, daemon_cli.daemonCommand(argToSlice(argv[2]))) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");

    if (!flags.json_mode) {
        try daemon_json.writeErrorJson(io, daemon_cli.daemonCommand(argToSlice(argv[2])), "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const socket_path = try protocol.resolveSocketPath(allocator, init.minimal.environ, flags.socket_arg);
    defer allocator.free(socket_path);

    if (std.mem.eql(u8, argToSlice(argv[2]), "_serve")) {
        var server = daemon_server.DaemonServer.init(allocator, io, socket_path);
        server.run() catch |err| {
            if (err == error.IpcBindFailed or err == error.InvalidSocketPath or err == error.SocketParentCreateFailed) {
                try daemon_json.writeErrorJson(io, "daemon _serve", "ERR_IPC_BIND_FAILED", "failed to bind daemon IPC socket", false);
                return recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED");
            }
            try daemon_json.writeErrorJson(io, "daemon _serve", "ERR_UNKNOWN_UNMAPPED", "daemon server failed with unmapped error", false);
            return recovery_policy.exitCodeFor("ERR_UNKNOWN_UNMAPPED");
        };
        return 0;
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "start")) {
        return daemon_cli.runDaemonStart(allocator, io, socket_path) catch |err| switch (err) {
            error.DaemonAlreadyRunning => recovery_policy.exitCodeFor("ERR_DAEMON_ALREADY_RUNNING"),
            error.IpcBindFailed => recovery_policy.exitCodeFor("ERR_IPC_BIND_FAILED"),
            error.IpcTimeout => recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT"),
            else => return err,
        };
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "status")) {
        return daemon_cli.runDaemonStatus(allocator, io, socket_path) catch |err| switch (err) {
            error.DaemonNotRunning => recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING"),
            error.IpcTimeout => recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT"),
            else => return err,
        };
    }

    if (std.mem.eql(u8, argToSlice(argv[2]), "stop")) {
        return daemon_cli.runDaemonStop(allocator, io, socket_path) catch |err| switch (err) {
            error.DaemonNotRunning => recovery_policy.exitCodeFor("ERR_DAEMON_NOT_RUNNING"),
            error.IpcTimeout => recovery_policy.exitCodeFor("ERR_IPC_TIMEOUT"),
            else => return err,
        };
    }

    try daemon_json.writeErrorJson(io, "daemon", "ERR_CLI_USAGE", "unsupported daemon subcommand", false);
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
                try daemon_json.writeErrorJson(io, command, "ERR_CLI_USAGE", "--socket is not supported for this command", false);
                return error.InvalidArgument;
            }
            if (i + 1 >= argv.len) {
                try daemon_json.writeErrorJson(io, command, "ERR_CLI_USAGE", "--socket requires a path", false);
                return error.InvalidArgument;
            }
            i += 1;
            parsed.socket_arg = argToSlice(argv[i]);
            continue;
        }

        try daemon_json.writeErrorJson(io, command, "ERR_CLI_USAGE", "unsupported flag", false);
        return error.InvalidArgument;
    }

    return parsed;
}

test "cli error usage code maps to taxonomy" {
    try std.testing.expectEqual(@as(u8, 2), recovery_policy.exitCodeFor("ERR_CLI_USAGE"));
}
fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

test "daemon command names are stable" {
    try std.testing.expectEqualStrings("daemon start", daemon_cli.daemonCommand("start"));
    try std.testing.expectEqualStrings("daemon", daemon_cli.daemonCommand("x"));
}
