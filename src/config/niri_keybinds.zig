const std = @import("std");

pub const RenderedKeybinds = struct {
    kdl: []u8,

    pub fn deinit(self: *RenderedKeybinds, allocator: std.mem.Allocator) void {
        allocator.free(self.kdl);
    }
};

pub const Conflict = struct {
    key: []const u8,
    action: []const u8,
    context: []const u8,
};

pub const KeybindStatus = struct {
    niri_detected: bool,
    config_path: ?[]u8,
    installed: bool,
    conflicts: []const Conflict,

    pub fn deinit(self: *KeybindStatus, allocator: std.mem.Allocator) void {
        if (self.config_path) |path| allocator.free(path);
        for (self.conflicts) |conflict| {
            allocator.free(conflict.key);
            allocator.free(conflict.action);
            allocator.free(conflict.context);
        }
        allocator.free(self.conflicts);
    }
};

pub const managed_keybinds_begin = "// BEGIN SHAULA MANAGED KEYBINDS";
pub const managed_keybinds_end = "// END SHAULA MANAGED KEYBINDS";

const keybind_specs = [_]struct {
    key: []const u8,
    subcommand: []const u8,
    title: []const u8,
    save: bool = false,
}{
    .{ .key = "CTRL+Shift+1", .subcommand = "quick", .title = "Shaula: Quick Capture" },
    .{ .key = "CTRL+Shift+2", .subcommand = "area", .title = "Shaula: Capture Area" },
    .{ .key = "CTRL+Shift+3", .subcommand = "fullscreen", .title = "Shaula: Capture Fullscreen", .save = true },
    .{ .key = "CTRL+Shift+4", .subcommand = "all-screens", .title = "Shaula: Capture All Screens", .save = true },
};

/// Render the Niri keybind block for Shaula capture shortcuts.
///
/// Contract constraints:
/// - uses `spawn`, not `spawn-sh`, because no shell features are needed.
/// - includes `--json`; capture commands reject non-JSON invocations with ERR_CLI_USAGE.
/// - CTRL+Shift+3/4 include `--save` so those shortcuts always leave a durable file.
/// - binary_path must be the absolute installed Shaula binary path.
pub fn renderKeybinds(allocator: std.mem.Allocator, binary_path: []const u8) !RenderedKeybinds {
    var out = std.ArrayList(u8).empty;
    errdefer out.deinit(allocator);

    try out.appendSlice(allocator, "binds {\n");
    for (keybind_specs) |spec| {
        if (spec.save) {
            try out.print(
                allocator,
                "    {s} repeat=false hotkey-overlay-title=\"{s}\" {{\n        spawn \"{s}\" \"capture\" \"{s}\" \"--json\" \"--save\";\n    }}\n\n",
                .{ spec.key, spec.title, binary_path, spec.subcommand },
            );
        } else {
            try out.print(
                allocator,
                "    {s} repeat=false hotkey-overlay-title=\"{s}\" {{\n        spawn \"{s}\" \"capture\" \"{s}\" \"--json\";\n    }}\n\n",
                .{ spec.key, spec.title, binary_path, spec.subcommand },
            );
        }
    }
    try out.appendSlice(allocator, "}\n");

    return .{ .kdl = try out.toOwnedSlice(allocator) };
}

/// Scan content for CTRL+Shift+[1-4] bindings outside the managed keybinds block.
///
/// Returns owned slices; caller must free each Conflict and the slice itself.
pub fn detectConflicts(allocator: std.mem.Allocator, content: []const u8) ![]const Conflict {
    var conflicts = std.ArrayList(Conflict).empty;
    errdefer {
        for (conflicts.items) |c| {
            allocator.free(c.key);
            allocator.free(c.action);
            allocator.free(c.context);
        }
        conflicts.deinit(allocator);
    }

    var in_managed_block = false;
    var line_number: u32 = 0;
    var lines = std.mem.splitScalar(u8, content, '\n');
    while (lines.next()) |line| {
        line_number += 1;
        const trimmed = std.mem.trim(u8, line, " \t\r");

        if (std.mem.indexOf(u8, trimmed, managed_keybinds_begin) != null) {
            in_managed_block = true;
            continue;
        }
        if (std.mem.indexOf(u8, trimmed, managed_keybinds_end) != null) {
            in_managed_block = false;
            continue;
        }
        if (in_managed_block) continue;

        const spec = matchKeybindConflict(trimmed) orelse continue;
        const context = try std.fmt.allocPrint(allocator, "line {d}: {s}", .{ line_number, trimmed });
        errdefer allocator.free(context);
        try conflicts.append(allocator, .{
            .key = try allocator.dupe(u8, spec.key),
            .action = try allocator.dupe(u8, spec.action),
            .context = context,
        });
    }

    return conflicts.toOwnedSlice(allocator);
}

fn matchKeybindConflict(trimmed_line: []const u8) ?struct { key: []const u8, action: []const u8 } {
    if (isKdlComment(trimmed_line)) return null;

    inline for (keybind_specs) |spec| {
        if (std.mem.indexOf(u8, trimmed_line, spec.key) != null) {
            const after_key = std.mem.indexOf(u8, trimmed_line, spec.key).? + spec.key.len;
            if (after_key < trimmed_line.len) {
                var rest = std.mem.trim(u8, trimmed_line[after_key..], " \t\r{;");
                rest = std.mem.trim(u8, rest, " \t\r};");
                if (rest.len > 0) {
                    return .{ .key = spec.key, .action = rest };
                }
            }
            return .{ .key = spec.key, .action = "(unknown)" };
        }
    }
    return null;
}

fn isKdlComment(line: []const u8) bool {
    const trimmed = std.mem.trim(u8, line, " \t");
    return trimmed.len >= 2 and trimmed[0] == '/' and trimmed[1] == '/';
}

test "render keybinds produces expected KDL" {
    var rendered = try renderKeybinds(std.testing.allocator, "/usr/bin/shaula");
    defer rendered.deinit(std.testing.allocator);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "binds {") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "CTRL+Shift+1") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "CTRL+Shift+2") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "CTRL+Shift+3") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "CTRL+Shift+4") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "spawn \"/usr/bin/shaula\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "\"capture\" \"quick\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "\"capture\" \"area\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "\"capture\" \"fullscreen\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "\"capture\" \"all-screens\"") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "hotkey-overlay-title") != null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "spawn-sh") == null);
    try std.testing.expect(std.mem.indexOf(u8, rendered.kdl, "--json") != null);
}

test "detect conflicts finds bindings outside managed block" {
    const content =
        \\CTRL+Shift+1 { toggle-overview; }
        \\// BEGIN SHAULA MANAGED KEYBINDS
        \\CTRL+Shift+2 { spawn "shaula" "capture" "area"; }
        \\// END SHAULA MANAGED KEYBINDS
        \\CTRL+Shift+3 { screenshot-screen; }
    ;
    const conflicts = try detectConflicts(std.testing.allocator, content);
    defer {
        for (conflicts) |c| {
            std.testing.allocator.free(c.key);
            std.testing.allocator.free(c.action);
            std.testing.allocator.free(c.context);
        }
        std.testing.allocator.free(conflicts);
    }
    try std.testing.expectEqual(@as(usize, 2), conflicts.len);
    try std.testing.expectEqualStrings("CTRL+Shift+1", conflicts[0].key);
    try std.testing.expectEqualStrings("CTRL+Shift+3", conflicts[1].key);
}

test "detect conflicts ignores bindings inside managed block" {
    const content =
        \\// BEGIN SHAULA MANAGED KEYBINDS
        \\CTRL+Shift+1 { spawn "shaula" "capture" "quick"; }
        \\// END SHAULA MANAGED KEYBINDS
    ;
    const conflicts = try detectConflicts(std.testing.allocator, content);
    defer {
        for (conflicts) |c| {
            std.testing.allocator.free(c.key);
            std.testing.allocator.free(c.action);
            std.testing.allocator.free(c.context);
        }
        std.testing.allocator.free(conflicts);
    }
    try std.testing.expectEqual(@as(usize, 0), conflicts.len);
}

test "detect conflicts ignores commented lines" {
    const content =
        \\//CTRL+Shift+1 { toggle-overview; }
        \\// CTRL+Shift+2 { screenshot; }
        \\CTRL+Shift+3 { screenshot-screen; }
    ;
    const conflicts = try detectConflicts(std.testing.allocator, content);
    defer {
        for (conflicts) |c| {
            std.testing.allocator.free(c.key);
            std.testing.allocator.free(c.action);
            std.testing.allocator.free(c.context);
        }
        std.testing.allocator.free(conflicts);
    }
    try std.testing.expectEqual(@as(usize, 1), conflicts.len);
    try std.testing.expectEqualStrings("CTRL+Shift+3", conflicts[0].key);
}
