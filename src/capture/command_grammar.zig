const std = @import("std");

const core_capture_mode = @import("../core/capture_mode.zig");
const command_json = @import("command_json.zig");

pub const CommandSpec = struct {
    command: []const u8,
    mode: []const u8,
    allow_dry_run: bool = false,
    allow_simulate_cancel: bool = false,
    allow_aspect: bool = false,
    allow_region_mode: bool = false,
    allow_window_id: bool = false,
};

/// Parse one capture command's CLI grammar into the provided flag struct.
///
/// Contract constraint: this Module owns supported-flag membership and
/// command-specific `ERR_CLI_USAGE` messages so public capture grammar does not
/// drift across individual command handlers.
pub fn parse(comptime Flags: type, io: std.Io, argv: []const [*:0]const u8, spec: CommandSpec) !Flags {
    var flags: Flags = .{};
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (try parseCommonFlag(io, spec, argv, &i, arg, &flags)) continue;

        if (std.mem.eql(u8, arg, "--dry-run")) {
            if (comptime @hasField(Flags, "dry_run")) if (spec.allow_dry_run) {
                flags.dry_run = true;
                continue;
            };
        }
        if (std.mem.eql(u8, arg, "--simulate-cancel")) {
            if (comptime @hasField(Flags, "simulate_cancel")) if (spec.allow_simulate_cancel) {
                flags.simulate_cancel = true;
                continue;
            };
        }
        if (std.mem.eql(u8, arg, "--aspect")) {
            if (comptime @hasField(Flags, "aspect")) if (spec.allow_aspect) {
                if (i + 1 >= argv.len) {
                    try usage(io, spec, "--aspect requires a value");
                    return error.CliUsage;
                }
                i += 1;
                flags.aspect = argToSlice(argv[i]);
                continue;
            };
        }
        if (std.mem.eql(u8, arg, "--region-mode")) {
            if (comptime @hasField(Flags, "region_capture_mode")) if (spec.allow_region_mode) {
                if (i + 1 >= argv.len) {
                    try usage(io, spec, "--region-mode requires live or frozen");
                    return error.CliUsage;
                }
                i += 1;
                flags.region_capture_mode = argToSlice(argv[i]);
                if (core_capture_mode.parseRegionCaptureMode(flags.region_capture_mode.?) == null) {
                    try usage(io, spec, "--region-mode requires live or frozen");
                    return error.CliUsage;
                }
                continue;
            };
        }
        if (std.mem.eql(u8, arg, "--frozen-region")) {
            if (comptime @hasField(Flags, "region_capture_mode")) if (spec.allow_region_mode) {
                flags.region_capture_mode = "frozen";
                continue;
            };
        }
        if (std.mem.eql(u8, arg, "--window-id")) {
            if (comptime @hasField(Flags, "window_id")) if (spec.allow_window_id) {
                if (i + 1 >= argv.len) {
                    try usage(io, spec, "--window-id requires a value");
                    return error.CliUsage;
                }
                i += 1;
                flags.window_id = argToSlice(argv[i]);
                continue;
            };
        }

        try usage(io, spec, "unsupported flag");
        return error.CliUsage;
    }
    return flags;
}

fn parseCommonFlag(
    io: std.Io,
    spec: CommandSpec,
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
            try usage(io, spec, "--output requires a path");
            return error.CliUsage;
        }
        index.* += 1;
        parsed.output = argToSlice(argv[index.*]);
        return true;
    }
    if (std.mem.eql(u8, arg, "--save")) {
        parsed.save = true;
        if (comptime @hasField(@TypeOf(parsed.*), "save_explicit")) parsed.save_explicit = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--copy")) {
        parsed.copy = true;
        if (comptime @hasField(@TypeOf(parsed.*), "copy_explicit")) parsed.copy_explicit = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--preview")) {
        parsed.preview = true;
        if (comptime @hasField(@TypeOf(parsed.*), "preview_explicit")) parsed.preview_explicit = true;
        return true;
    }
    if (std.mem.eql(u8, arg, "--no-preview")) {
        parsed.preview = false;
        if (comptime @hasField(@TypeOf(parsed.*), "preview_explicit")) parsed.preview_explicit = true;
        return true;
    }
    return false;
}

fn usage(io: std.Io, spec: CommandSpec, message: []const u8) !void {
    try command_json.writeErrorJson(io, spec.command, "ERR_CLI_USAGE", message, false, spec.mode, null, false, &.{});
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
