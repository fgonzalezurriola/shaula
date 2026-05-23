const std = @import("std");

const CInt = c_int;

extern fn g_malloc(n_bytes: usize) ?[*]u8;

/// Allocates a GLib-owned, null-terminated string for C helper return values.
///
/// Contract constraint: callers must release the returned pointer with
/// `g_free`; every result is explicitly null-terminated for GTK/GLib APIs.
pub fn dupZ(value: []const u8) [*:0]u8 {
    const memory = g_malloc(value.len + 1) orelse @panic("g_malloc failed");
    @memcpy(memory[0..value.len], value);
    memory[value.len] = 0;
    return @ptrCast(memory);
}

pub fn allocPrintZ(comptime fmt: []const u8, args: anytype) [*:0]u8 {
    var buffer: [4096]u8 = undefined;
    const text = std.fmt.bufPrint(&buffer, fmt, args) catch @panic("format overflow");
    return dupZ(text);
}

pub fn joinPathZ(parts: []const []const u8) [*:0]u8 {
    var len: usize = 0;
    for (parts, 0..) |part, index| {
        len += part.len;
        if (index + 1 < parts.len) len += 1;
    }

    const memory = g_malloc(len + 1) orelse @panic("g_malloc failed");
    var cursor: usize = 0;
    for (parts, 0..) |part, index| {
        @memcpy(memory[cursor .. cursor + part.len], part);
        cursor += part.len;
        if (index + 1 < parts.len) {
            memory[cursor] = '/';
            cursor += 1;
        }
    }
    memory[cursor] = 0;
    return @ptrCast(memory);
}

pub fn exitedZero(status: CInt) bool {
    return (status & 0x7f) == 0 and ((status >> 8) & 0xff) == 0;
}
