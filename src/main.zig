const std = @import("std");

pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");
pub const protocol_module = @import("ipc/protocol.zig");
pub const compositor_runtime_module = @import("compositor/runtime.zig");

const preflight_probe = @import("preflight/probe.zig");
const capabilities_probe = @import("capabilities/probe.zig");
const capture_command = @import("capture/command.zig");
const history_command = @import("history/command.zig");
const clipboard_command = @import("clipboard/command.zig");
const preview_command = @import("preview/command.zig");
const notify_command = @import("notify/command.zig");
const errors_command = @import("errors/command.zig");
const config_command = @import("config/command.zig");
const doctor_command = @import("doctor/command.zig");
const explore_command = @import("explore/command.zig");
const settings_command = @import("settings/command.zig");
const directory_command = @import("directory/command.zig");
const setup_command = @import("setup/command.zig");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

pub fn main(init: std.process.Init) !u8 {
    const io = init.io;
    const allocator = init.gpa;
    const argv = init.minimal.args.vector;

    if (argv.len < 2) {
        try writeBasicError(io, "", "ERR_CLI_USAGE", "usage: shaula <capture|preview|notify|config|settings|setup|directory|doctor|explore|preflight|capabilities|history|clipboard|errors> ...", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const family = argToSlice(argv[1]);

    if (std.mem.eql(u8, family, "preflight")) {
        const flags = parseFlags(io, argv, 2, "preflight") catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try writeBasicError(io, "preflight", "ERR_CLI_USAGE", "--json is required", false);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        }
        return preflight_probe.run(allocator, io, init.minimal.environ);
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
                try writeBasicError(io, "capabilities list", "ERR_CLI_USAGE", "usage: shaula capabilities [list] --json", false);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
        }

        const flags = parseFlags(io, argv, flags_start, flags_command) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        if (!flags.json_mode) {
            try writeBasicError(io, flags_command, "ERR_CLI_USAGE", "--json is required", false);
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

    if (std.mem.eql(u8, family, "notify")) {
        return notify_command.run(allocator, io, argv);
    }

    if (std.mem.eql(u8, family, "config")) {
        return config_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "settings")) {
        return settings_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "setup")) {
        return setup_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "directory")) {
        return directory_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "doctor")) {
        return doctor_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "explore")) {
        return explore_command.run(allocator, io, init.minimal.environ, argv);
    }

    if (std.mem.eql(u8, family, "errors")) {
        return errors_command.run(allocator, io, argv);
    }

    try writeBasicError(io, family, "ERR_CLI_USAGE", "unsupported command family", false);
    return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
}

const ParsedFlags = struct {
    json_mode: bool,
};

fn parseFlags(io: std.Io, argv: []const [*:0]const u8, start: usize, command: []const u8) !ParsedFlags {
    var parsed: ParsedFlags = .{ .json_mode = false };

    var i: usize = start;
    while (i < argv.len) : (i += 1) {
        const current = argToSlice(argv[i]);
        if (std.mem.eql(u8, current, "--json")) {
            parsed.json_mode = true;
            continue;
        }

        try writeBasicError(io, command, "ERR_CLI_USAGE", "unsupported flag", false);
        return error.InvalidArgument;
    }

    return parsed;
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn jsonSpan(value: []const u8) c.ShaulaJsonSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn writeBasicError(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
) !void {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);

    const status = c.shaula_json_basic_error_build(
        std.Io.Timestamp.now(io, .real).toSeconds(),
        jsonSpan(command),
        jsonSpan(code),
        jsonSpan(message),
        @intFromBool(retryable),
        jsonSpan("{}"),
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}
