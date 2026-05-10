const std = @import("std");
const grammar = @import("command_grammar.zig");

pub const AreaFlags = struct {
    json_mode: bool = false,
    dry_run: bool = false,
    simulate_cancel: bool = false,
    save: bool = false,
    copy: bool = false,
    preview: ?bool = null,
    aspect: ?[]const u8 = null,
    output: ?[]const u8 = null,
    region_capture_mode: ?[]const u8 = null,
};

pub const FullscreenFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    preview: ?bool = null,
    output: ?[]const u8 = null,
};

pub const AllScreensFlags = FullscreenFlags;

pub const FocusedFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = true,
    preview: ?bool = null,
    output: ?[]const u8 = null,
};

pub const WindowFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    preview: ?bool = null,
    output: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
};

pub const PreviousAreaFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    preview: ?bool = null,
    output: ?[]const u8 = null,
};

pub const AllInOneFlags = AreaFlags;

/// Parse `capture area` flags and emit deterministic CLI usage errors.
pub fn parseAreaFlags(io: std.Io, argv: []const [*:0]const u8) !AreaFlags {
    return grammar.parse(AreaFlags, io, argv, regionSpec("capture area", "area"));
}

pub fn parseFullscreenFlags(io: std.Io, argv: []const [*:0]const u8) !FullscreenFlags {
    return grammar.parse(FullscreenFlags, io, argv, simpleSpec("capture fullscreen", "fullscreen"));
}

pub fn parseAllScreensFlags(io: std.Io, argv: []const [*:0]const u8) !AllScreensFlags {
    return grammar.parse(AllScreensFlags, io, argv, simpleSpec("capture all-screens", "all-screens"));
}

/// Parse `capture focused` flags and emit deterministic CLI usage errors.
pub fn parseFocusedFlags(io: std.Io, argv: []const [*:0]const u8) !FocusedFlags {
    return grammar.parse(FocusedFlags, io, argv, simpleSpec("capture focused", "focused"));
}

pub fn parseWindowFlags(io: std.Io, argv: []const [*:0]const u8) !WindowFlags {
    var spec = simpleSpec("capture window", "window");
    spec.allow_window_id = true;
    return grammar.parse(WindowFlags, io, argv, spec);
}

pub fn parsePreviousAreaFlags(io: std.Io, argv: []const [*:0]const u8) !PreviousAreaFlags {
    return grammar.parse(PreviousAreaFlags, io, argv, simpleSpec("capture previous-area", "previous-area"));
}

pub fn parseAllInOneFlags(io: std.Io, argv: []const [*:0]const u8) !AllInOneFlags {
    return grammar.parse(AllInOneFlags, io, argv, regionSpec("capture all-in-one", "all-in-one"));
}

fn regionSpec(command: []const u8, mode: []const u8) grammar.CommandSpec {
    return .{
        .command = command,
        .mode = mode,
        .allow_dry_run = true,
        .allow_simulate_cancel = true,
        .allow_aspect = true,
        .allow_region_mode = true,
    };
}

fn simpleSpec(command: []const u8, mode: []const u8) grammar.CommandSpec {
    return .{ .command = command, .mode = mode };
}

pub fn resolvePreviewDefault(mode: []const u8, explicit: ?bool) bool {
    if (explicit) |value| return value;
    return std.mem.eql(u8, mode, "area") or std.mem.eql(u8, mode, "all-in-one");
}
