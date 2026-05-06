const std = @import("std");
const core_capture_mode = @import("../core/capture_mode.zig");
const command_json = @import("command_json.zig");

pub const AreaFlags = struct {
    json_mode: bool = false,
    dry_run: bool = false,
    simulate_cancel: bool = false,
    save: bool = false,
    copy: bool = true,
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

pub const FocusedFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
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
    var flags: AreaFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture area", "area", argv, &i, arg, &flags)) continue;
        if (std.mem.eql(u8, arg, "--dry-run")) {
            flags.dry_run = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--simulate-cancel")) {
            flags.simulate_cancel = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--aspect")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--aspect requires a value", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.aspect = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--region-mode")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--region-mode requires live or frozen", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.region_capture_mode = argToSlice(argv[i]);
            if (core_capture_mode.parseRegionCaptureMode(flags.region_capture_mode.?) == null) {
                try command_json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--region-mode requires live or frozen", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            continue;
        }
        if (std.mem.eql(u8, arg, "--frozen-region")) {
            flags.region_capture_mode = "frozen";
            continue;
        }
        try command_json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "unsupported flag", false, "area", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

pub fn parseFullscreenFlags(io: std.Io, argv: []const [*:0]const u8) !FullscreenFlags {
    var flags: FullscreenFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture fullscreen", "fullscreen", argv, &i, arg, &flags)) continue;
        try command_json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "unsupported flag", false, "fullscreen", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

/// Parse `capture focused` flags and emit deterministic CLI usage errors.
pub fn parseFocusedFlags(io: std.Io, argv: []const [*:0]const u8) !FocusedFlags {
    var flags: FocusedFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture focused", "focused", argv, &i, arg, &flags)) continue;
        try command_json.writeErrorJson(io, "capture focused", "ERR_CLI_USAGE", "unsupported flag", false, "focused", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

pub fn parseWindowFlags(io: std.Io, argv: []const [*:0]const u8) !WindowFlags {
    var flags: WindowFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture window", "window", argv, &i, arg, &flags)) continue;
        if (std.mem.eql(u8, arg, "--window-id")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--window-id requires a value", false, "window", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.window_id = argToSlice(argv[i]);
            continue;
        }
        try command_json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "unsupported flag", false, "window", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

pub fn parsePreviousAreaFlags(io: std.Io, argv: []const [*:0]const u8) !PreviousAreaFlags {
    var flags: PreviousAreaFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture previous-area", "previous-area", argv, &i, arg, &flags)) continue;
        try command_json.writeErrorJson(io, "capture previous-area", "ERR_CLI_USAGE", "unsupported flag", false, "previous-area", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

pub fn parseAllInOneFlags(io: std.Io, argv: []const [*:0]const u8) !AllInOneFlags {
    var flags: AllInOneFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, "capture all-in-one", "all-in-one", argv, &i, arg, &flags)) continue;
        if (std.mem.eql(u8, arg, "--dry-run")) {
            flags.dry_run = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--simulate-cancel")) {
            flags.simulate_cancel = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--aspect")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "--aspect requires a value", false, "all-in-one", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.aspect = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--region-mode")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "--region-mode requires live or frozen", false, "all-in-one", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.region_capture_mode = argToSlice(argv[i]);
            if (core_capture_mode.parseRegionCaptureMode(flags.region_capture_mode.?) == null) {
                try command_json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "--region-mode requires live or frozen", false, "all-in-one", null, false, &.{});
                return error.CliUsage;
            }
            continue;
        }
        if (std.mem.eql(u8, arg, "--frozen-region")) {
            flags.region_capture_mode = "frozen";
            continue;
        }
        try command_json.writeErrorJson(io, "capture all-in-one", "ERR_CLI_USAGE", "unsupported flag", false, "all-in-one", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

/// Parse capture flags shared by every capture subcommand.
///
/// Contract constraint: unsupported flags stay with each mode parser so the
/// existing command-specific `ERR_CLI_USAGE` messages remain unchanged.
fn parseCommonFlag(
    io: std.Io,
    command: []const u8,
    mode: []const u8,
    argv: []const [*:0]const u8,
    index: *usize,
    arg: []const u8,
    parsed: anytype,
) !bool {
    if (std.mem.eql(u8, arg, "--json")) {
        parsed.json_mode = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--output")) {
        if (index.* + 1 >= argv.len) {
            try command_json.writeErrorJson(io, command, "ERR_CLI_USAGE", "--output requires a path", false, mode, null, false, &.{});
            return error.CliUsage;
        }
        index.* += 1;
        parsed.output = argToSlice(argv[index.*]);
        return true;
    }
    if (std.mem.eql(u8, arg, "--save")) {
        parsed.save = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--copy")) {
        parsed.copy = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--preview")) {
        parsed.preview = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--no-preview")) {
        parsed.preview = false;
        return true;
    }
    return false;
}

pub fn resolvePreviewDefault(mode: []const u8, explicit: ?bool) bool {
    if (explicit) |value| return value;
    return std.mem.eql(u8, mode, "area") or std.mem.eql(u8, mode, "all-in-one");
}
