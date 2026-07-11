const std = @import("std");

const lifecycle = @import("lifecycle.zig");
const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};
const json = @import("command_json.zig");
const c = @cImport({
    @cInclude("capabilities/runtime.h");
    @cInclude("core/capture_mode.h");
    @cInclude("errors/taxonomy.h");
});

fn captureModeSpan(value: []const u8) c.ShaulaCaptureModeSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn resolveRuntime(environ: std.process.Environ) c.ShaulaRuntimeDecision {
    var environment: c.ShaulaCapabilitiesEnvironment = .{
        .compositor = .{
            .shaula_compositor = envValue(environ, "SHAULA_COMPOSITOR"),
            .niri_socket = envValue(environ, "NIRI_SOCKET"),
            .xdg_current_desktop = envValue(environ, "XDG_CURRENT_DESKTOP"),
            .xdg_session_desktop = envValue(environ, "XDG_SESSION_DESKTOP"),
            .wayland_display = envValue(environ, "WAYLAND_DISPLAY"),
        },
        .capture_backend = envValue(environ, "SHAULA_CAPTURE_BACKEND"),
        .capture_force_portal = envValue(environ, "SHAULA_CAPTURE_FORCE_PORTAL"),
        .portal_available = envValue(environ, "SHAULA_PORTAL_AVAILABLE"),
        .portal_window_capable = envValue(environ, "SHAULA_PORTAL_WINDOW_CAPABLE"),
    };
    var runtime: c.ShaulaRuntimeDecision = std.mem.zeroes(c.ShaulaRuntimeDecision);
    if (c.shaula_capabilities_resolve(&environment, &runtime) != c.SHAULA_CAPABILITIES_STATUS_OK) unreachable;
    return runtime;
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

    const runtime = resolveRuntime(environ);

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
