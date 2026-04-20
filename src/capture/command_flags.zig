const std = @import("std");
const command_json = @import("command_json.zig");

pub const AreaFlags = struct {
    json_mode: bool = false,
    dry_run: bool = false,
    simulate_cancel: bool = false,
    save: bool = false,
    copy: bool = false,
    aspect: ?[]const u8 = null,
    output: ?[]const u8 = null,
};

pub const FullscreenFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    output: ?[]const u8 = null,
};

pub const WindowFlags = struct {
    json_mode: bool = false,
    save: bool = false,
    copy: bool = false,
    output: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
};

pub fn parseAreaFlags(io: std.Io, argv: []const [*:0]const u8) !AreaFlags {
    var flags: AreaFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
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
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture area", "ERR_CLI_USAGE", "--output requires a path", false, "area", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
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
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "--output requires a path", false, "fullscreen", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
            continue;
        }

        try command_json.writeErrorJson(io, "capture fullscreen", "ERR_CLI_USAGE", "unsupported flag", false, "fullscreen", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

pub fn parseWindowFlags(io: std.Io, argv: []const [*:0]const u8) !WindowFlags {
    var flags: WindowFlags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--output")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--output requires a path", false, "window", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.output = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--window-id")) {
            if (i + 1 >= argv.len) {
                try command_json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "--window-id requires a value", false, "window", null, false, &.{});
                return error.CliUsage;
            }
            i += 1;
            flags.window_id = argToSlice(argv[i]);
            continue;
        }
        if (std.mem.eql(u8, arg, "--save")) {
            flags.save = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--copy")) {
            flags.copy = true;
            continue;
        }

        try command_json.writeErrorJson(io, "capture window", "ERR_CLI_USAGE", "unsupported flag", false, "window", null, false, &.{});
        return error.CliUsage;
    }
    return flags;
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
