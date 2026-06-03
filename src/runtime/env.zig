const std = @import("std");

/// Shared environment parsing for runtime-facing modules.
///
/// Contract constraints: returned slices are borrowed from `std.process.Environ`
/// and must not outlive it; invalid boolean/numeric values resolve to null or
/// caller-provided defaults so deterministic `ERR_*` mapping stays at callers.
pub fn slice(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    if (environ.getPosix(key)) |value| return std.mem.sliceTo(value, 0);
    return null;
}

pub fn trimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    const raw = slice(environ, key) orelse return null;
    const value = std.mem.trim(u8, raw, " \t\r\n");
    if (value.len == 0) return null;
    return value;
}

pub fn flag(environ: std.process.Environ, key: []const u8) ?bool {
    const raw = trimmed(environ, key) orelse return null;
    if (std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes")) return true;
    if (std.mem.eql(u8, raw, "0") or std.ascii.eqlIgnoreCase(raw, "false") or std.ascii.eqlIgnoreCase(raw, "no")) return false;
    return null;
}

pub fn flagEnabled(environ: std.process.Environ, key: []const u8) bool {
    return flag(environ, key) orelse false;
}

pub fn unsignedOrDefault(comptime Int: type, environ: std.process.Environ, key: []const u8, default_value: Int) Int {
    const raw = trimmed(environ, key) orelse return default_value;
    const value = std.fmt.parseInt(Int, raw, 10) catch return default_value;
    return value;
}

pub fn firstDesktopToken(value: []const u8) ?[]const u8 {
    var it_colon = std.mem.splitScalar(u8, value, ':');
    while (it_colon.next()) |chunk| {
        var it_semicolon = std.mem.splitScalar(u8, chunk, ';');
        while (it_semicolon.next()) |subchunk| {
            const token = std.mem.trim(u8, subchunk, " \t\r\n");
            if (token.len > 0) return token;
        }
    }
    return null;
}
