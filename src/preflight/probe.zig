const std = @import("std");
const backend_contract = @import("../capture/backends/capture_backend_contract.zig");
const cli_json = @import("../cli/json.zig");
const compositor_runtime = @import("../compositor/runtime.zig");
const runtime_capabilities = @import("../capabilities/runtime.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");

pub const PreflightError = error{UnsupportedCompositor};

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, socket_arg: ?[]const u8) !u8 {
    const runtime = runtime_capabilities.resolve(allocator, io, environ);
    if (!runtime.compositor_supported) {
        try writeErrorJson(io, "preflight", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const socket_path = try protocol.resolveSocketPath(allocator, environ, socket_arg);
    defer allocator.free(socket_path);

    const wayland_ready = environ.getPosix("WAYLAND_DISPLAY") != null;
    if (!wayland_ready) {
        try writeErrorJson(io, "preflight", "ERR_PREFLIGHT_ENV_NOT_READY", "wayland environment is not ready", true, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_PREFLIGHT_ENV_NOT_READY");
    }

    const ipc_ready = blk: {
        std.Io.Dir.accessAbsolute(io, socket_path, .{}) catch break :blk false;
        break :blk true;
    };

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const warning = runtime.usesPortalBackend();
    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"preflight\",\"timestamp\":\"{s}\",\"compositor\":\"{s}\",\"ready\":true,\"ipc\":{{\"socket\":\"{s}\",\"ready\":{s}}},\"result\":{{\"compositor\":\"{s}\",\"wayland\":true,\"ipc_ready\":{s},\"backend\":\"{s}\",\"portal_available\":{s}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            ts,
            runtime.compositor.label,
            socket_path,
            if (ipc_ready) "true" else "false",
            runtime.compositor.label,
            if (ipc_ready) "true" else "false",
            runtime.backendUsedLabel(),
            if (runtime.portal_available) "true" else "false",
            if (warning) "[\"" ++ backend_contract.warning_portal_fallback ++ "\"]" else "[]",
        },
    );
    try stdout.interface.flush();
    return 0;
}

pub fn detectCompositor(environ: std.process.Environ) []const u8 {
    return compositor_runtime.detect(environ).label;
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool, detected_compositor: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    const compositor_json = try cli_json.stringAlloc(allocator, detected_compositor);
    defer allocator.free(compositor_json);
    const details_json = try std.fmt.allocPrint(allocator, "{{\"detected_compositor\":{s}}}", .{compositor_json});
    defer allocator.free(details_json);
    try cli_json.writeErrorWithDetails(io, command, code, message, retryable, details_json);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}

test "detect compositor is niri when SHAULA_COMPOSITOR is niri" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("SHAULA_COMPOSITOR", "niri");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("niri", detectCompositor(environ));
}

test "detect compositor unsupported on non-niri marker" {
    var env = std.process.EnvMap.init(std.testing.allocator);
    defer env.deinit();

    try env.put("XDG_CURRENT_DESKTOP", "sway");
    const environ = try std.process.Environ.initFromEnvMap(std.testing.allocator, &env);
    defer environ.deinit(std.testing.allocator);

    try std.testing.expectEqualStrings("sway", detectCompositor(environ));
}
