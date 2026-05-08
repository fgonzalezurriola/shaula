const std = @import("std");

const core_capture_mode = @import("../core/capture_mode.zig");
const lifecycle = @import("lifecycle.zig");
const recovery_policy = @import("../recovery/policy.zig");
const json = @import("command_json.zig");

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
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "usage: shaula capture <area|fullscreen|all-screens|focused|window|previous-area> --json", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    const requested_mode = core_capture_mode.parseCliToken(subcommand) orelse {
        try json.writeErrorJson(io, "capture", "ERR_CLI_USAGE", "unsupported capture subcommand", false, null, null, false, &.{});
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    };

    return switch (requested_mode) {
        .area => lifecycle.runArea(allocator, io, environ, argv),
        .fullscreen => lifecycle.runFullscreen(allocator, io, environ, argv),
        .all_screens => lifecycle.runAllScreens(allocator, io, environ, argv),
        .focused => lifecycle.runFocused(allocator, io, environ, argv),
        .window => lifecycle.runWindow(allocator, io, environ, argv),
        .previous_area => lifecycle.runPreviousArea(allocator, io, environ, argv),
        .all_in_one => lifecycle.runAllInOne(allocator, io, environ, argv),
    };
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
