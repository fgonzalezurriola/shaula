const std = @import("std");

const Args = struct {
    backend: []const u8 = "",
    mode: []const u8 = "",
    geometry: ?[]const u8 = null,
    output: []const u8 = "",
};

const Size = struct {
    width: u32,
    height: u32,
};

pub fn main(init: std.process.Init) !u8 {
    const allocator = init.gpa;
    const io = init.io;

    var args_iterator = try std.process.Args.Iterator.initAllocator(init.minimal.args, allocator);
    defer args_iterator.deinit();

    const args = parseArgs(&args_iterator) catch return 2;
    if (args.backend.len == 0 or args.mode.len == 0 or args.output.len == 0) return 2;

    if (std.fs.path.dirname(args.output)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var file = try std.Io.Dir.createFileAbsolute(io, args.output, .{ .truncate = true });
    defer file.close(io);
    try writePng(allocator, io, init.minimal.environ, file, args);
    return 0;
}

fn parseArgs(iterator: *std.process.Args.Iterator) !Args {
    var result: Args = .{};
    _ = iterator.skip();
    while (iterator.next()) |arg| {
        if (std.mem.eql(u8, arg, "--backend")) {
            result.backend = iterator.next() orelse return error.InvalidArgs;
        } else if (std.mem.eql(u8, arg, "--mode")) {
            result.mode = iterator.next() orelse return error.InvalidArgs;
        } else if (std.mem.eql(u8, arg, "--geometry")) {
            result.geometry = iterator.next() orelse return error.InvalidArgs;
        } else if (std.mem.eql(u8, arg, "--output")) {
            result.output = iterator.next() orelse return error.InvalidArgs;
        }
    }
    return result;
}

fn dimensions(args: Args) Size {
    if (std.mem.eql(u8, args.mode, "area")) {
        if (args.geometry) |geometry| {
            if (parseGeometrySize(geometry)) |size| return size;
        }
        return .{ .width = 640, .height = 360 };
    }
    if (std.mem.eql(u8, args.mode, "window")) {
        return .{ .width = 1280, .height = 720 };
    }
    return .{ .width = 1920, .height = 1080 };
}

fn parseGeometrySize(geometry: []const u8) ?Size {
    const space = std.mem.indexOfScalar(u8, geometry, ' ') orelse return null;
    const size = geometry[space + 1 ..];
    const x = std.mem.indexOfScalar(u8, size, 'x') orelse return null;
    const width = std.fmt.parseInt(u32, size[0..x], 10) catch return null;
    const height = std.fmt.parseInt(u32, size[x + 1 ..], 10) catch return null;
    if (width == 0 or height == 0) return null;
    return .{ .width = width, .height = height };
}

fn writePng(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, file: std.Io.File, args: Args) !void {
    const size = dimensions(args);
    const row_len: usize = 1 + @as(usize, size.width) * 4;
    const raw_len = row_len * @as(usize, size.height);
    const raw = try allocator.alloc(u8, raw_len);
    defer allocator.free(raw);

    const panel_visible = envFlag(environ, "SHAULA_CAPTURE_INJECT_PANEL_MARKER") and !panelHidden(io, environ);
    var offset: usize = 0;
    var y: u32 = 0;
    while (y < size.height) : (y += 1) {
        raw[offset] = 0;
        offset += 1;
        var x: u32 = 0;
        while (x < size.width) : (x += 1) {
            const color = if (panel_visible and x < 16 and y < 16)
                [_]u8{ 255, 0, 255, 255 }
            else
                colorForPixel(x, y);
            @memcpy(raw[offset .. offset + 4], &color);
            offset += 4;
        }
    }

    var idat: std.ArrayList(u8) = .empty;
    defer idat.deinit(allocator);
    try writeZlibStored(allocator, &idat, raw);

    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(allocator);
    try out.appendSlice(allocator, "\x89PNG\r\n\x1a\n");

    var ihdr: [13]u8 = undefined;
    std.mem.writeInt(u32, ihdr[0..4], size.width, .big);
    std.mem.writeInt(u32, ihdr[4..8], size.height, .big);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    try appendChunk(allocator, &out, "IHDR", &ihdr);
    try appendChunk(allocator, &out, "IDAT", idat.items);
    try appendChunk(allocator, &out, "IEND", "");

    try file.writeStreamingAll(io, out.items);
}

fn colorForPixel(x: u32, y: u32) [4]u8 {
    const palette = [_][4]u8{
        .{ 255, 0, 0, 255 },
        .{ 0, 255, 0, 255 },
        .{ 0, 128, 255, 255 },
        .{ 255, 255, 0, 255 },
        .{ 255, 0, 255, 255 },
        .{ 0, 255, 255, 255 },
    };
    const idx = ((x / 32) + (y / 32)) % palette.len;
    return palette[idx];
}

fn writeZlibStored(allocator: std.mem.Allocator, out: *std.ArrayList(u8), raw: []const u8) !void {
    try out.appendSlice(allocator, &.{ 0x78, 0x01 });
    var index: usize = 0;
    while (index < raw.len) {
        const remaining = raw.len - index;
        const chunk_len: u16 = @intCast(@min(remaining, 65535));
        const final: u8 = if (index + chunk_len == raw.len) 1 else 0;
        try out.append(allocator, final);
        try out.append(allocator, @intCast(chunk_len & 0xff));
        try out.append(allocator, @intCast(chunk_len >> 8));
        const nlen = ~chunk_len;
        try out.append(allocator, @intCast(nlen & 0xff));
        try out.append(allocator, @intCast(nlen >> 8));
        try out.appendSlice(allocator, raw[index .. index + chunk_len]);
        index += chunk_len;
    }
    var adler: std.hash.Adler32 = .{};
    adler.update(raw);
    var buffer: [4]u8 = undefined;
    std.mem.writeInt(u32, &buffer, adler.adler, .big);
    try out.appendSlice(allocator, &buffer);
}

fn appendChunk(allocator: std.mem.Allocator, out: *std.ArrayList(u8), tag: *const [4:0]u8, payload: []const u8) !void {
    var length: [4]u8 = undefined;
    std.mem.writeInt(u32, &length, @intCast(payload.len), .big);
    try out.appendSlice(allocator, &length);
    try out.appendSlice(allocator, tag[0..4]);
    try out.appendSlice(allocator, payload);

    var crc = std.hash.Crc32.init();
    crc.update(tag[0..4]);
    crc.update(payload);
    var crc_bytes: [4]u8 = undefined;
    std.mem.writeInt(u32, &crc_bytes, crc.final(), .big);
    try out.appendSlice(allocator, &crc_bytes);
}

fn envFlag(environ: std.process.Environ, name: []const u8) bool {
    const raw_z = environ.getPosix(name) orelse return false;
    const raw = std.mem.sliceTo(raw_z, 0);
    return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
}

fn panelHidden(io: std.Io, environ: std.process.Environ) bool {
    if (envFlag(environ, "SHAULA_PANEL_HIDDEN")) return true;
    if (environ.getPosix("SHAULA_PANEL_STATE")) |state_z| {
        if (std.ascii.eqlIgnoreCase(std.mem.sliceTo(state_z, 0), "hidden")) return true;
    }
    if (environ.getPosix("SHAULA_PANEL_HIDDEN_TOKEN_FILE")) |path_z| {
        const path = std.mem.sliceTo(path_z, 0);
        if (path.len > 0) {
            std.Io.Dir.accessAbsolute(io, path, .{}) catch return false;
            return true;
        }
    }
    return false;
}
