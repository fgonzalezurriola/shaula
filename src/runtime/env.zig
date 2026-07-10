const std = @import("std");

/// Temporary Zig facade over the C runtime environment ABI.
///
/// Contract constraints: lookup still uses the caller-provided
/// `std.process.Environ`, so tests and explicit child environments do not fall
/// back to process-global `getenv`. All trimming, boolean, numeric, and desktop
/// token behavior is delegated to `env.c`. Returned slices borrow the supplied
/// environment storage and must not outlive or survive mutation of that storage.
/// Delete this facade when the remaining Zig callers move to C.
const CSpan = extern struct {
    data: ?[*]const u8,
    length: usize,
};

const status_valid: i32 = 1;

extern fn shaula_env_value_slice(value: ?[*:0]const u8, out: *CSpan) i32;
extern fn shaula_env_value_trimmed(value: ?[*:0]const u8, out: *CSpan) i32;
extern fn shaula_env_value_flag(value: ?[*:0]const u8, out_value: *i32) i32;
extern fn shaula_env_value_unsigned_or_default(
    value: ?[*:0]const u8,
    max_value: u64,
    default_value: u64,
) u64;
extern fn shaula_env_first_desktop_token(value: CSpan, out: *CSpan) i32;

comptime {
    if (@sizeOf(i32) != 4) @compileError("runtime env status ABI requires 32-bit i32");
    if (@sizeOf(u64) != 8) @compileError("runtime env unsigned ABI requires 64-bit u64");
    if (@sizeOf(CSpan) != @sizeOf(?[*]const u8) + @sizeOf(usize)) {
        @compileError("runtime env span ABI layout changed");
    }
}

fn borrowedSlice(span: CSpan) []const u8 {
    const data = span.data orelse unreachable;
    return data[0..span.length];
}

fn cValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

pub fn slice(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: CSpan = .{ .data = null, .length = 0 };
    if (shaula_env_value_slice(cValue(environ, key), &result) != status_valid) {
        return null;
    }
    return borrowedSlice(result);
}

pub fn trimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: CSpan = .{ .data = null, .length = 0 };
    if (shaula_env_value_trimmed(cValue(environ, key), &result) != status_valid) {
        return null;
    }
    return borrowedSlice(result);
}

pub fn flag(environ: std.process.Environ, key: []const u8) ?bool {
    var result: i32 = 0;
    if (shaula_env_value_flag(cValue(environ, key), &result) != status_valid) {
        return null;
    }
    return result != 0;
}

pub fn flagEnabled(environ: std.process.Environ, key: []const u8) bool {
    return flag(environ, key) orelse false;
}

pub fn unsignedOrDefault(comptime Int: type, environ: std.process.Environ, key: []const u8, default_value: Int) Int {
    const int_info = switch (@typeInfo(Int)) {
        .int => |info| info,
        else => @compileError("unsignedOrDefault requires an unsigned integer type"),
    };
    comptime {
        if (int_info.signedness != .unsigned or int_info.bits > 64) {
            @compileError("unsignedOrDefault supports unsigned integer types up to 64 bits");
        }
    }

    const parsed = shaula_env_value_unsigned_or_default(
        cValue(environ, key),
        @intCast(std.math.maxInt(Int)),
        @intCast(default_value),
    );
    return @intCast(parsed);
}

pub fn firstDesktopToken(value: []const u8) ?[]const u8 {
    var result: CSpan = .{ .data = null, .length = 0 };
    const input: CSpan = .{ .data = value.ptr, .length = value.len };
    if (shaula_env_first_desktop_token(input, &result) != status_valid) {
        return null;
    }
    return borrowedSlice(result);
}

test "facade preserves supplied environment lookup and borrowed lifetime" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();
    try map.put("FIRST", "first");
    try map.put("SECOND", "second");
    try map.put("EMPTY", "");
    try map.put("PADDED", " \tvalue\r\n");
    try map.put("FLAG", "YeS");
    try map.put("U8_MAX", "255");

    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);
    const environ: std.process.Environ = .{ .block = block };

    const first = slice(environ, "FIRST").?;
    try std.testing.expectEqualStrings("second", slice(environ, "SECOND").?);
    try std.testing.expectEqualStrings("first", first);
    try std.testing.expectEqualStrings("", slice(environ, "EMPTY").?);
    try std.testing.expect(trimmed(environ, "EMPTY") == null);
    try std.testing.expectEqualStrings("value", trimmed(environ, "PADDED").?);
    try std.testing.expectEqual(true, flag(environ, "FLAG").?);
    try std.testing.expectEqual(@as(u8, 255), unsignedOrDefault(u8, environ, "U8_MAX", 7));
    try std.testing.expect(slice(environ, "MISSING") == null);
}
