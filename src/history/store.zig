const std = @import("std");

pub const HistoryEntry = struct {
    path: []const u8,
    mime: []const u8,
    width: u32,
    height: u32,
    backend_used: []const u8,
    timestamp: []const u8,
};

const history_dir = "/tmp/shaula/history";
const history_latest = "/tmp/shaula/history/latest.v1";
pub const default_top_n: usize = 20;

pub fn storeLatest(io: std.Io, entry: HistoryEntry) !void {
    try std.Io.Dir.cwd().createDirPath(io, history_dir);

    const existing = std.Io.Dir.cwd().readFileAlloc(io, history_latest, std.heap.smp_allocator, .unlimited) catch null;
    defer if (existing) |content| std.heap.smp_allocator.free(content);

    var file = try std.Io.Dir.createFileAbsolute(io, history_latest, .{ .truncate = true });
    defer file.close(io);

    var writer = file.writer(io, &.{});
    try writer.interface.print("{s}|{s}|{d}|{d}|{s}|{s}\n", .{
        entry.path,
        entry.mime,
        entry.width,
        entry.height,
        entry.backend_used,
        entry.timestamp,
    });

    if (existing) |content| {
        var lines = std.mem.tokenizeAny(u8, content, "\r\n");
        var kept: usize = 1;
        while (lines.next()) |line| {
            if (line.len == 0) continue;
            if (kept >= default_top_n) break;

            try writer.interface.print("{s}\n", .{line});
            kept += 1;
        }
    }

    try writer.interface.flush();
}

pub fn listEntries(allocator: std.mem.Allocator, io: std.Io) ![]HistoryEntry {
    const content = std.Io.Dir.cwd().readFileAlloc(io, history_latest, allocator, .unlimited) catch {
        return allocator.alloc(HistoryEntry, 0);
    };
    defer allocator.free(content);

    if (std.mem.trim(u8, content, "\r\n \t").len == 0) {
        return allocator.alloc(HistoryEntry, 0);
    }

    var list = try std.ArrayList(HistoryEntry).initCapacity(allocator, default_top_n);
    errdefer {
        for (list.items) |entry| freeEntry(allocator, entry);
        list.deinit(allocator);
    }

    var lines = std.mem.tokenizeAny(u8, content, "\r\n");
    while (lines.next()) |line| {
        if (line.len == 0) continue;
        if (list.items.len >= default_top_n) break;

        const parsed = try parseEntryAlloc(allocator, line) orelse continue;
        list.append(allocator, parsed) catch |err| {
            freeEntry(allocator, parsed);
            return err;
        };
    }

    return list.toOwnedSlice(allocator);
}

pub fn listLatest(allocator: std.mem.Allocator, io: std.Io) ![]HistoryEntry {
    return listEntries(allocator, io);
}

fn parseEntryAlloc(allocator: std.mem.Allocator, line: []const u8) !?HistoryEntry {
    var parts = std.mem.splitScalar(u8, line, '|');
    const path = parts.next() orelse return null;
    const mime = parts.next() orelse return null;
    const width_raw = parts.next() orelse return null;
    const height_raw = parts.next() orelse return null;
    const backend = parts.next() orelse return null;
    const ts = parts.next() orelse return null;

    if (path.len == 0 or mime.len == 0 or backend.len == 0 or ts.len == 0) return null;

    const width = std.fmt.parseInt(u32, width_raw, 10) catch 0;
    const height = std.fmt.parseInt(u32, height_raw, 10) catch 0;

    const path_copy = try allocator.dupe(u8, path);
    errdefer allocator.free(path_copy);
    const mime_copy = try allocator.dupe(u8, mime);
    errdefer allocator.free(mime_copy);
    const backend_copy = try allocator.dupe(u8, backend);
    errdefer allocator.free(backend_copy);
    const ts_copy = try allocator.dupe(u8, ts);
    errdefer allocator.free(ts_copy);

    return .{
        .path = path_copy,
        .mime = mime_copy,
        .width = width,
        .height = height,
        .backend_used = backend_copy,
        .timestamp = ts_copy,
    };
}

fn freeEntry(allocator: std.mem.Allocator, entry: HistoryEntry) void {
    allocator.free(entry.path);
    allocator.free(entry.mime);
    allocator.free(entry.backend_used);
    allocator.free(entry.timestamp);
}

pub fn deinitEntries(allocator: std.mem.Allocator, entries: []HistoryEntry) void {
    for (entries) |entry| {
        freeEntry(allocator, entry);
    }
    allocator.free(entries);
}
