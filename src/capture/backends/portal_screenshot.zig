const std = @import("std");

const backend_contract = @import("capture_backend_contract.zig");
const c = @cImport({
    @cInclude("runtime/env.h");
    @cInclude("runtime/helper_resolution.h");
    @cInclude("runtime/process_exec.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn envFlag(environ: std.process.Environ, key: []const u8) ?bool {
    var value: i32 = 0;
    if (c.shaula_env_value_flag(envValue(environ, key), &value) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return value != 0;
}

fn helperSpan(value: []const u8) c.ShaulaRuntimeHelperSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn resolveSiblingHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    env_var: []const u8,
    binary_name: []const u8,
) ![]u8 {
    const executable_dir = std.process.executableDirPathAlloc(io, allocator) catch null;
    defer if (executable_dir) |path| allocator.free(path);

    var owned: c.ShaulaRuntimeHelperOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_helper_owned_path_clear(&owned);
    const status = c.shaula_runtime_helper_resolve(
        envValue(environ, env_var),
        if (executable_dir) |path| helperSpan(path) else .{ .data = null, .length = 0 },
        helperSpan(binary_name),
        &owned,
    );
    return switch (status) {
        c.SHAULA_RUNTIME_HELPER_STATUS_OK => allocator.dupe(u8, owned.data[0..owned.length]),
        c.SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT => error.InvalidPath,
        c.SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        else => error.HelperResolutionFailed,
    };
}

pub const backend_label = backend_contract.backend_portal_screenshot;
pub const helper_env_var = "SHAULA_PORTAL_SCREENSHOT_HELPER_BIN";
pub const helper_binary = "shaula-portal-screenshot";

pub const CapabilityProbe = struct {
    available: bool = false,
    window_capable: bool = false,
};

/// Resolve the installed portal helper used by the portal backend.
///
/// Contract constraint: the portal backend must use the same helper argv shape as
/// QA runtime helpers, while keeping an explicit override for local diagnostics.
pub fn resolveHelperBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    return resolveSiblingHelper(allocator, io, environ, helper_env_var, helper_binary);
}

/// Probe xdg-desktop-portal Screenshot availability without opening UI.
///
/// Environment overrides exist so tests and remote shells can make deterministic
/// support decisions without requiring a live user session bus. Runtime capture
/// still validates the backend at execution time and maps failures to `ERR_*`.
pub fn detectCapabilities(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) CapabilityProbe {
    if (envFlag(environ, "SHAULA_PORTAL_AVAILABLE")) |available| {
        return .{
            .available = available,
            .window_capable = available and (envFlag(environ, "SHAULA_PORTAL_WINDOW_CAPABLE") orelse false),
        };
    }

    const version = portalProperty(allocator, io, "version") orelse return .{};
    defer allocator.free(version);

    const targets = portalProperty(allocator, io, "AvailableTargets");
    defer if (targets) |raw| allocator.free(raw);

    return .{
        .available = true,
        .window_capable = if (targets) |raw| portalTargetsIncludeWindow(raw) else false,
    };
}

fn portalProperty(allocator: std.mem.Allocator, io: std.Io, property: []const u8) ?[]u8 {
    _ = io;
    const argv = [_]c.ShaulaProcessSpan{
        processSpan("gdbus"),
        processSpan("call"),
        processSpan("--session"),
        processSpan("--timeout"),
        processSpan("2"),
        processSpan("--dest"),
        processSpan("org.freedesktop.portal.Desktop"),
        processSpan("--object-path"),
        processSpan("/org/freedesktop/portal/desktop"),
        processSpan("--method"),
        processSpan("org.freedesktop.DBus.Properties.Get"),
        processSpan("org.freedesktop.portal.Screenshot"),
        processSpan(property),
    };
    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    if (c.shaula_process_run(.{ .items = &argv, .length = argv.len }, null, 2048, 2048, &output) != c.SHAULA_PROCESS_STATUS_OK) {
        return null;
    }
    if (output.term_kind != c.SHAULA_PROCESS_TERM_EXITED or output.term_value != 0) return null;
    return allocator.dupe(u8, output.stdout_bytes.data[0..output.stdout_bytes.length]) catch null;
}

fn portalTargetsIncludeWindow(raw: []const u8) bool {
    const value = parseLastUnsigned(raw) orelse return false;
    const window: u64 = 2;
    const active_window: u64 = 8;
    return (value & window) != 0 or (value & active_window) != 0;
}

fn parseLastUnsigned(raw: []const u8) ?u64 {
    var end = raw.len;
    while (end > 0) {
        while (end > 0 and !std.ascii.isDigit(raw[end - 1])) end -= 1;
        if (end == 0) return null;
        var start = end;
        while (start > 0 and std.ascii.isDigit(raw[start - 1])) start -= 1;
        return std.fmt.parseInt(u64, raw[start..end], 10) catch null;
    }
    return null;
}
