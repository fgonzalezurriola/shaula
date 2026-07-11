const std = @import("std");

const lifecycle = @import("lifecycle.zig");
const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};
const json = @import("command_json.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const c = @cImport({
    @cInclude("core/capture_mode.h");
    @cInclude("errors/taxonomy.h");
});

fn captureModeSpan(value: []const u8) c.ShaulaCaptureModeSpan {
    return .{ .data = value.ptr, .length = value.len };
}

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
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "usage: shaula capture <quick|area|fullscreen|all-screens|window|previous-area> --json", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    const requested_mode = c.shaula_capture_mode_parse_cli_token(captureModeSpan(subcommand));
    if (requested_mode == c.SHAULA_CAPTURE_MODE_INVALID) {
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "unsupported capture subcommand", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const runtime = runtime_capabilities.resolve(allocator, io, environ);

    return switch (requested_mode) {
        c.SHAULA_CAPTURE_MODE_QUICK => lifecycle.runQuick(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_AREA => lifecycle.runArea(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_FULLSCREEN => lifecycle.runFullscreen(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_ALL_SCREENS => lifecycle.runAllScreens(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_FOCUSED => lifecycle.runFocused(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_WINDOW => lifecycle.runWindow(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA => lifecycle.runPreviousArea(allocator, io, environ, runtime, argv),
        c.SHAULA_CAPTURE_MODE_ALL_IN_ONE => lifecycle.runAllInOne(allocator, io, environ, runtime, argv),
        else => unreachable,
    };
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
