const std = @import("std");

const backend_contract = @import("capture_backend_contract.zig");
const env = @import("../runtime/env.zig");
const helper_resolution = @import("../runtime/helper_resolution.zig");
const process_exec = @import("../runtime/process_exec.zig");

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
    return helper_resolution.resolveSiblingHelper(allocator, io, environ, helper_env_var, helper_binary);
}

/// Probe xdg-desktop-portal Screenshot availability without opening UI.
///
/// Environment overrides exist so tests and remote shells can make deterministic
/// support decisions without requiring a live user session bus. Runtime capture
/// still validates the backend at execution time and maps failures to `ERR_*`.
pub fn detectCapabilities(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) CapabilityProbe {
    if (env.flag(environ, "SHAULA_PORTAL_AVAILABLE")) |available| {
        return .{
            .available = available,
            .window_capable = available and (env.flag(environ, "SHAULA_PORTAL_WINDOW_CAPABLE") orelse false),
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
    const result = process_exec.run(allocator, io, &.{
        "gdbus",
        "call",
        "--session",
        "--timeout",
        "2",
        "--dest",
        "org.freedesktop.portal.Desktop",
        "--object-path",
        "/org/freedesktop/portal/desktop",
        "--method",
        "org.freedesktop.DBus.Properties.Get",
        "org.freedesktop.portal.Screenshot",
        property,
    }, 2048, 2048) catch return null;
    defer result.deinit(allocator);

    if (!result.exitedZero()) return null;
    return allocator.dupe(u8, result.stdout) catch null;
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
