const std = @import("std");

const cli_json = @import("../cli/json.zig");
const compositor_runtime = @import("../compositor/runtime.zig");
const focused_output = @import("../compositor/focused_output.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = struct {
    fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};
const c = @cImport({
    @cInclude("errors/taxonomy.h");
    @cInclude("runtime/process_exec.h");
});

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

const warning_inventory_unavailable = "explore_inventory_unavailable";

const ExploreFlags = struct {
    json_mode: bool = false,
    brief: bool = false,
};

const Inventory = struct {
    outputs_json: []u8,
    workspaces_json: []u8,
    windows_json: []u8,
    focused_workspace_json: []u8,
    focused_window_json: []u8,
    recommended_capture_json: []u8,
    inventory_available: bool,

    fn deinit(self: Inventory, allocator: std.mem.Allocator) void {
        allocator.free(self.outputs_json);
        allocator.free(self.workspaces_json);
        allocator.free(self.windows_json);
        allocator.free(self.focused_workspace_json);
        allocator.free(self.focused_window_json);
        allocator.free(self.recommended_capture_json);
    }
};

/// Emit read-only desktop inventory for agent visual loops.
///
/// Contract constraints: this command never captures pixels or mutates desktop
/// state. Runtime inventory failures remain non-fatal and surface as warning
/// tokens so agents can fall back to ordinary capture commands deterministically.
pub fn run(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    argv: []const [*:0]const u8,
) !u8 {
    const flags = parseFlags(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (!flags.json_mode) {
        try cli_json.writeBasicError(io, "explore", "ERR_CLI_USAGE", "--json is required", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const compositor = compositor_runtime.detect(environ);
    const focused_output_name = focused_output.resolveName(allocator, io, environ) catch null;
    defer if (focused_output_name) |value| allocator.free(value);

    const inventory = try resolveInventory(allocator, io, compositor, focused_output_name, flags.brief);
    defer inventory.deinit(allocator);

    const ts = try cli_json.nowIso8601(allocator, io);
    defer allocator.free(ts);
    const ts_json = try cli_json.stringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const kind_json = try cli_json.stringAlloc(allocator, @tagName(compositor.kind));
    defer allocator.free(kind_json);
    const label_json = try cli_json.stringAlloc(allocator, compositor.label);
    defer allocator.free(label_json);
    const focused_output_json = try cli_json.nullableStringAlloc(allocator, focused_output_name);
    defer allocator.free(focused_output_json);
    const warnings_json = try cli_json.warningsAlloc(
        allocator,
        if (inventory.inventory_available) &.{} else &.{warning_inventory_unavailable},
    );
    defer allocator.free(warnings_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    if (flags.brief) {
        try stdout.interface.print(
            "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"explore\",\"timestamp\":{s},\"result\":{{\"compositor\":{{\"kind\":{s},\"label\":{s}}},\"focused\":{{\"output_id\":{s},\"workspace_id\":{s},\"window_id\":{s}}},\"recommended_capture\":{s}}},\"warnings\":{s}}}\n",
            .{
                protocol.contract_version,
                ts_json,
                kind_json,
                label_json,
                focused_output_json,
                inventory.focused_workspace_json,
                inventory.focused_window_json,
                inventory.recommended_capture_json,
                warnings_json,
            },
        );
    } else {
        try stdout.interface.print(
            "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"explore\",\"timestamp\":{s},\"result\":{{\"compositor\":{{\"kind\":{s},\"label\":{s}}},\"focused\":{{\"output_id\":{s},\"workspace_id\":{s},\"window_id\":{s}}},\"outputs\":{s},\"workspaces\":{s},\"windows\":{s},\"recommended_capture\":{s}}},\"warnings\":{s}}}\n",
            .{
                protocol.contract_version,
                ts_json,
                kind_json,
                label_json,
                focused_output_json,
                inventory.focused_workspace_json,
                inventory.focused_window_json,
                inventory.outputs_json,
                inventory.workspaces_json,
                inventory.windows_json,
                inventory.recommended_capture_json,
                warnings_json,
            },
        );
    }
    try stdout.interface.flush();
    return 0;
}

fn parseFlags(io: std.Io, argv: []const [*:0]const u8) !ExploreFlags {
    var flags: ExploreFlags = .{};
    var i: usize = 2;
    while (i < argv.len) : (i += 1) {
        const arg = std.mem.sliceTo(argv[i], 0);
        if (std.mem.eql(u8, arg, "--json")) {
            flags.json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--brief")) {
            flags.brief = true;
            continue;
        }
        try cli_json.writeBasicError(io, "explore", "ERR_CLI_USAGE", "usage: shaula explore --json [--brief]", false);
        return error.InvalidArgument;
    }
    return flags;
}

fn resolveInventory(
    allocator: std.mem.Allocator,
    io: std.Io,
    compositor: compositor_runtime.Detection,
    focused_output_name: ?[]const u8,
    brief: bool,
) !Inventory {
    if (compositor.kind != .niri) {
        return emptyInventory(allocator, focused_output_name, false);
    }

    const raw_outputs = runNiriJson(allocator, io, "outputs") catch return emptyInventory(allocator, focused_output_name, false);
    defer allocator.free(raw_outputs);
    if (brief) {
        const outputs = std.json.parseFromSlice(std.json.Value, allocator, raw_outputs, .{}) catch return emptyInventory(allocator, focused_output_name, false);
        defer outputs.deinit();
        return inventoryFromValues(allocator, outputs.value, null, null, focused_output_name, true);
    }

    const raw_workspaces = runNiriJson(allocator, io, "workspaces") catch return emptyInventory(allocator, focused_output_name, false);
    defer allocator.free(raw_workspaces);
    const raw_windows = runNiriJson(allocator, io, "windows") catch return emptyInventory(allocator, focused_output_name, false);
    defer allocator.free(raw_windows);

    const outputs = std.json.parseFromSlice(std.json.Value, allocator, raw_outputs, .{}) catch return emptyInventory(allocator, focused_output_name, false);
    defer outputs.deinit();
    const workspaces = std.json.parseFromSlice(std.json.Value, allocator, raw_workspaces, .{}) catch return emptyInventory(allocator, focused_output_name, false);
    defer workspaces.deinit();
    const windows = std.json.parseFromSlice(std.json.Value, allocator, raw_windows, .{}) catch return emptyInventory(allocator, focused_output_name, false);
    defer windows.deinit();

    return inventoryFromValues(allocator, outputs.value, workspaces.value, windows.value, focused_output_name, true);
}

fn emptyInventory(allocator: std.mem.Allocator, focused_output_name: ?[]const u8, available: bool) !Inventory {
    const recommended = try recommendedCaptureJson(allocator, focused_output_name);
    errdefer allocator.free(recommended);
    const outputs_json = try allocator.dupe(u8, "[]");
    errdefer allocator.free(outputs_json);
    const workspaces_json = try allocator.dupe(u8, "[]");
    errdefer allocator.free(workspaces_json);
    const windows_json = try allocator.dupe(u8, "[]");
    errdefer allocator.free(windows_json);
    const focused_workspace_json = try allocator.dupe(u8, "null");
    errdefer allocator.free(focused_workspace_json);
    const focused_window_json = try allocator.dupe(u8, "null");
    errdefer allocator.free(focused_window_json);
    return .{
        .outputs_json = outputs_json,
        .workspaces_json = workspaces_json,
        .windows_json = windows_json,
        .focused_workspace_json = focused_workspace_json,
        .focused_window_json = focused_window_json,
        .recommended_capture_json = recommended,
        .inventory_available = available,
    };
}

fn runNiriJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8) ![]u8 {
    _ = io;
    const argv = [_]c.ShaulaProcessSpan{
        processSpan("niri"),
        processSpan("msg"),
        processSpan("-j"),
        processSpan(command),
    };
    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    if (c.shaula_process_run(.{ .items = &argv, .length = argv.len }, null, 1024 * 1024, 16 * 1024, &output) != c.SHAULA_PROCESS_STATUS_OK) {
        return error.NiriInventoryUnavailable;
    }
    if (output.term_kind != c.SHAULA_PROCESS_TERM_EXITED or output.term_value != 0) return error.NiriInventoryUnavailable;
    const stdout = output.stdout_bytes.data[0..output.stdout_bytes.length];
    return try allocator.dupe(u8, std.mem.trim(u8, stdout, " \t\r\n"));
}

fn inventoryFromValues(
    allocator: std.mem.Allocator,
    outputs: std.json.Value,
    workspaces: ?std.json.Value,
    windows: ?std.json.Value,
    focused_output_name: ?[]const u8,
    available: bool,
) !Inventory {
    const outputs_json = try outputsJson(allocator, outputs, focused_output_name);
    errdefer allocator.free(outputs_json);
    const workspaces_json = if (workspaces) |value| try workspacesJson(allocator, value) else try allocator.dupe(u8, "[]");
    errdefer allocator.free(workspaces_json);
    const windows_json = if (windows) |value| try windowsJson(allocator, value, workspaces) else try allocator.dupe(u8, "[]");
    errdefer allocator.free(windows_json);
    const focused_workspace_json = if (workspaces) |value| try focusedWorkspaceJson(allocator, value) else try allocator.dupe(u8, "null");
    errdefer allocator.free(focused_workspace_json);
    const focused_window_json = if (windows) |value| try focusedWindowJson(allocator, value) else try allocator.dupe(u8, "null");
    errdefer allocator.free(focused_window_json);
    const recommended_capture_json = try recommendedCaptureJson(allocator, focused_output_name);
    errdefer allocator.free(recommended_capture_json);

    return .{
        .outputs_json = outputs_json,
        .workspaces_json = workspaces_json,
        .windows_json = windows_json,
        .focused_workspace_json = focused_workspace_json,
        .focused_window_json = focused_window_json,
        .recommended_capture_json = recommended_capture_json,
        .inventory_available = available,
    };
}

fn outputsJson(allocator: std.mem.Allocator, value: std.json.Value, focused_output_name: ?[]const u8) ![]u8 {
    var list = std.ArrayList(u8).empty;
    defer list.deinit(allocator);
    try list.append(allocator, '[');

    var first = true;
    if (value == .array) {
        for (value.array.items) |item| {
            if (item != .object) continue;
            if (!first) try list.append(allocator, ',');
            first = false;
            try appendOutputJson(allocator, &list, item, null, focused_output_name);
        }
    } else if (value == .object) {
        var iter = value.object.iterator();
        while (iter.next()) |entry| {
            if (!first) try list.append(allocator, ',');
            first = false;
            try appendOutputJson(allocator, &list, entry.value_ptr.*, entry.key_ptr.*, focused_output_name);
        }
    }

    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

fn appendOutputJson(allocator: std.mem.Allocator, list: *std.ArrayList(u8), value: std.json.Value, fallback_name: ?[]const u8, focused_output_name: ?[]const u8) !void {
    const name = stringField(value, "name") orelse stringField(value, "id") orelse fallback_name orelse "";
    const id_json = try cli_json.stringAlloc(allocator, name);
    defer allocator.free(id_json);
    const name_json = try cli_json.stringAlloc(allocator, name);
    defer allocator.free(name_json);
    const focused = boolField(value, "focused") orelse if (focused_output_name) |focused_name| std.mem.eql(u8, focused_name, name) else false;
    const geometry = try geometryJson(allocator, value);
    defer allocator.free(geometry);
    const logical = objectField(value, "logical");
    const scale = numberField(value, "scale") orelse numberField(value, "logical_scale") orelse if (logical) |l| numberField(l, "scale") else null;
    const scale_json = if (scale) |s| try numberJson(allocator, s) else try allocator.dupe(u8, "null");
    defer allocator.free(scale_json);

    try list.print(
        allocator,
        "{{\"id\":{s},\"name\":{s},\"focused\":{s},\"geometry\":{s},\"scale\":{s}}}",
        .{ id_json, name_json, boolJson(focused), geometry, scale_json },
    );
}

fn workspacesJson(allocator: std.mem.Allocator, value: std.json.Value) ![]u8 {
    var list = std.ArrayList(u8).empty;
    defer list.deinit(allocator);
    try list.append(allocator, '[');
    if (value == .array) {
        for (value.array.items, 0..) |item, index| {
            if (item != .object) continue;
            if (index != 0) try list.append(allocator, ',');
            try appendWorkspaceJson(allocator, &list, item);
        }
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

fn appendWorkspaceJson(allocator: std.mem.Allocator, list: *std.ArrayList(u8), value: std.json.Value) !void {
    const id = intField(value, "id") orelse intField(value, "idx") orelse 0;
    const name_json = try nullableFieldStringJson(allocator, value, "name");
    defer allocator.free(name_json);
    const output_json = try nullableFieldStringJson(allocator, value, "output");
    defer allocator.free(output_json);
    const focused = boolField(value, "focused") orelse boolField(value, "is_focused") orelse false;
    const active = boolField(value, "active") orelse boolField(value, "is_active") orelse focused;

    try list.print(
        allocator,
        "{{\"id\":{d},\"name\":{s},\"output_id\":{s},\"focused\":{s},\"active\":{s}}}",
        .{ id, name_json, output_json, boolJson(focused), boolJson(active) },
    );
}

fn windowsJson(allocator: std.mem.Allocator, value: std.json.Value, workspaces: ?std.json.Value) ![]u8 {
    var list = std.ArrayList(u8).empty;
    defer list.deinit(allocator);
    try list.append(allocator, '[');
    if (value == .array) {
        for (value.array.items, 0..) |item, index| {
            if (item != .object) continue;
            if (index != 0) try list.append(allocator, ',');
            try appendWindowJson(allocator, &list, item, workspaces);
        }
    }
    try list.append(allocator, ']');
    return list.toOwnedSlice(allocator);
}

fn appendWindowJson(allocator: std.mem.Allocator, list: *std.ArrayList(u8), value: std.json.Value, workspaces: ?std.json.Value) !void {
    const id = intField(value, "id") orelse 0;
    const app_id_json = try nullableFieldStringJson(allocator, value, "app_id");
    defer allocator.free(app_id_json);
    const title_json = try nullableFieldStringJson(allocator, value, "title");
    defer allocator.free(title_json);
    const workspace_id = intField(value, "workspace_id");
    const workspace_id_json = if (workspace_id) |v| try intJson(allocator, v) else try allocator.dupe(u8, "null");
    defer allocator.free(workspace_id_json);
    const output_id_json = if (workspace_id) |workspace_value| try workspaceOutputJson(allocator, workspaces, workspace_value) else try allocator.dupe(u8, "null");
    defer allocator.free(output_id_json);
    const focused = boolField(value, "focused") orelse boolField(value, "is_focused") orelse false;

    try list.print(
        allocator,
        "{{\"id\":{d},\"app_id\":{s},\"title\":{s},\"workspace_id\":{s},\"output_id\":{s},\"focused\":{s}}}",
        .{ id, app_id_json, title_json, workspace_id_json, output_id_json, boolJson(focused) },
    );
}

fn workspaceOutputJson(allocator: std.mem.Allocator, workspaces: ?std.json.Value, workspace_id: i64) ![]u8 {
    const value = workspaces orelse return allocator.dupe(u8, "null");
    if (value != .array) return allocator.dupe(u8, "null");
    for (value.array.items) |item| {
        if (item != .object) continue;
        const id = intField(item, "id") orelse intField(item, "idx") orelse continue;
        if (id == workspace_id) return nullableFieldStringJson(allocator, item, "output");
    }
    return allocator.dupe(u8, "null");
}

fn focusedWorkspaceJson(allocator: std.mem.Allocator, value: std.json.Value) ![]u8 {
    if (value != .array) return allocator.dupe(u8, "null");
    for (value.array.items) |item| {
        if (item != .object) continue;
        const focused = boolField(item, "focused") orelse boolField(item, "is_focused") orelse false;
        if (focused) return intJson(allocator, intField(item, "id") orelse intField(item, "idx") orelse 0);
    }
    return allocator.dupe(u8, "null");
}

fn focusedWindowJson(allocator: std.mem.Allocator, value: std.json.Value) ![]u8 {
    if (value != .array) return allocator.dupe(u8, "null");
    for (value.array.items) |item| {
        if (item != .object) continue;
        const focused = boolField(item, "focused") orelse boolField(item, "is_focused") orelse false;
        if (focused) return intJson(allocator, intField(item, "id") orelse 0);
    }
    return allocator.dupe(u8, "null");
}

fn recommendedCaptureJson(allocator: std.mem.Allocator, focused_output_name: ?[]const u8) ![]u8 {
    if (focused_output_name) |name| {
        const id_json = try cli_json.stringAlloc(allocator, name);
        defer allocator.free(id_json);
        return std.fmt.allocPrint(allocator, "{{\"mode\":\"output\",\"id\":{s},\"reason\":\"focused_output\"}}", .{id_json});
    }
    return allocator.dupe(u8, "{\"mode\":\"fullscreen\",\"id\":null,\"reason\":\"focused_output_unavailable\"}");
}

fn geometryJson(allocator: std.mem.Allocator, value: std.json.Value) ![]u8 {
    const logical = objectField(value, "logical") orelse objectField(value, "geometry") orelse value;
    const x = intField(logical, "x") orelse 0;
    const y = intField(logical, "y") orelse 0;
    const width = intField(logical, "width") orelse return allocator.dupe(u8, "null");
    const height = intField(logical, "height") orelse return allocator.dupe(u8, "null");
    return std.fmt.allocPrint(allocator, "{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}}", .{ x, y, width, height });
}

fn nullableFieldStringJson(allocator: std.mem.Allocator, value: std.json.Value, key: []const u8) ![]u8 {
    if (stringField(value, key)) |text| return cli_json.stringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

fn objectField(value: std.json.Value, key: []const u8) ?std.json.Value {
    if (value != .object) return null;
    if (value.object.get(key)) |field| {
        if (field == .object) return field;
    }
    return null;
}

fn stringField(value: std.json.Value, key: []const u8) ?[]const u8 {
    if (value != .object) return null;
    if (value.object.get(key)) |field| {
        if (field == .string) return field.string;
    }
    return null;
}

fn boolField(value: std.json.Value, key: []const u8) ?bool {
    if (value != .object) return null;
    if (value.object.get(key)) |field| {
        if (field == .bool) return field.bool;
    }
    return null;
}

fn intField(value: std.json.Value, key: []const u8) ?i64 {
    if (value != .object) return null;
    if (value.object.get(key)) |field| {
        return switch (field) {
            .integer => |v| v,
            else => null,
        };
    }
    return null;
}

fn numberField(value: std.json.Value, key: []const u8) ?std.json.Value {
    if (value != .object) return null;
    if (value.object.get(key)) |field| {
        return switch (field) {
            .integer, .float => field,
            else => null,
        };
    }
    return null;
}

fn boolJson(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn intJson(allocator: std.mem.Allocator, value: i64) ![]u8 {
    return std.fmt.allocPrint(allocator, "{d}", .{value});
}

fn numberJson(allocator: std.mem.Allocator, value: std.json.Value) ![]u8 {
    return switch (value) {
        .integer => |v| std.fmt.allocPrint(allocator, "{d}", .{v}),
        .float => |v| std.fmt.allocPrint(allocator, "{d}", .{v}),
        else => allocator.dupe(u8, "null"),
    };
}
