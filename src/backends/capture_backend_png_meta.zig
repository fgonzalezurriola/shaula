const std = @import("std");

pub fn resolveCaptureDimensions(
    allocator: std.mem.Allocator,
    io: std.Io,
    output_path: []const u8,
) !struct { width: u32, height: u32 } {
    const header = try std.Io.Dir.cwd().readFileAlloc(io, output_path, allocator, .unlimited);
    defer allocator.free(header);

    if (header.len < 24) return error.BackendUnavailable;
    if (!std.mem.eql(u8, header[0..8], "\x89PNG\r\n\x1a\n")) return error.BackendUnavailable;
    if (!std.mem.eql(u8, header[12..16], "IHDR")) return error.BackendUnavailable;

    const width = readBigEndianU32(header[16], header[17], header[18], header[19]);
    const height = readBigEndianU32(header[20], header[21], header[22], header[23]);
    if (width == 0 or height == 0) return error.BackendUnavailable;

    return .{ .width = width, .height = height };
}

fn readBigEndianU32(a: u8, b: u8, c: u8, d: u8) u32 {
    return (@as(u32, a) << 24) | (@as(u32, b) << 16) | (@as(u32, c) << 8) | @as(u32, d);
}
