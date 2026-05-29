const std = @import("std");

const selection = @import("../selection/selection.zig");
const capture_types = @import("../capture/types.zig");

const HelperStatus = enum {
    ok,
    cancel,
    @"error",
};

const HelperAction = enum {
    capture,
    cancel,
};

const HelperGeometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

const HelperOutput = struct {
    name: ?[]const u8 = null,
    x: i32 = 0,
    y: i32 = 0,
    width: u32 = 0,
    height: u32 = 0,
};

const HelperError = struct {
    code: []const u8,
    message: []const u8,
};

const HelperEnvelope = struct {
    status: HelperStatus,
    aspect: ?[]const u8 = null,
    geometry: ?HelperGeometry = null,
    local_geometry: ?HelperGeometry = null,
    output: ?HelperOutput = null,
    action: ?HelperAction = null,
    @"error": ?HelperError = null,
};

pub const LocalSelection = struct {
    output_name: ?[]u8 = null,
    output_origin_x: i32 = 0,
    output_origin_y: i32 = 0,
    output_width: u32,
    output_height: u32,
    geometry: capture_types.AreaGeometry,

    pub fn deinit(self: LocalSelection, allocator: std.mem.Allocator) void {
        if (self.output_name) |name| allocator.free(name);
    }
};

pub const LocalGeometry = capture_types.AreaGeometry;

pub const AspectOverride = union(enum) {
    missing,
    free,
    value: []u8,

    pub fn deinit(self: AspectOverride, allocator: std.mem.Allocator) void {
        switch (self) {
            .value => |aspect| allocator.free(aspect),
            .missing, .free => {},
        }
    }
};

/// Parse helper stdio envelope v1 into the selection contract.
///
/// Contract constraints:
/// - `status:"ok"` requires `action:"capture"` and valid non-zero geometry.
/// - `status:"cancel"` and `status:"error"` map to `cancelled=true`.
/// - malformed payloads are treated as cancelled so caller boundaries emit
///   deterministic `ERR_SELECTION_CANCELLED` or overlay-specific `ERR_*`.
pub fn parseSelectionEnvelope(
    allocator: std.mem.Allocator,
    payload: []const u8,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
) selection.SelectionResult {
    const parsed = std.json.parseFromSlice(HelperEnvelope, allocator, payload, .{}) catch {
        return cancelledSelection(mode, constraint);
    };
    defer parsed.deinit();

    const envelope = parsed.value;
    switch (envelope.status) {
        .ok => {
            if (envelope.action == null or envelope.action.? != .capture) {
                return cancelledSelection(mode, constraint);
            }

            const geometry = envelope.geometry orelse return cancelledSelection(mode, constraint);
            if (geometry.width == 0 or geometry.height == 0) {
                return cancelledSelection(mode, constraint);
            }

            return resultFromGeometry(mode, constraint, geometry, false);
        },
        .cancel, .@"error" => {
            if (validEnvelopeGeometry(envelope.geometry)) |geometry| {
                return resultFromGeometry(mode, constraint, geometry, true);
            }
            return cancelledSelection(mode, constraint);
        },
    }
}

/// Extract the optional final aspect emitted by the native helper.
///
/// Contract: `"Free"` is a known unlocked aspect, valid `W:H` returns an owned
/// value, and malformed or absent aspect fields are ignored as missing.
pub fn parseAspectOverrideAlloc(allocator: std.mem.Allocator, payload: []const u8) !AspectOverride {
    const parsed = std.json.parseFromSlice(HelperEnvelope, allocator, payload, .{}) catch return .missing;
    defer parsed.deinit();

    const aspect = parsed.value.aspect orelse return .missing;
    if (freeAspect(aspect)) return .free;
    if (validAspect(aspect)) |valid| return .{ .value = try allocator.dupe(u8, valid) };
    return .missing;
}

/// Extract output-local confirmed geometry for overlay runtime state only.
///
/// Contract: this must never replace `geometry`, which remains the helper JSON
/// backend capture contract in compositor-layout coordinates.
pub fn parseConfirmedLocalSelectionAlloc(allocator: std.mem.Allocator, payload: []const u8) !?LocalSelection {
    const parsed = std.json.parseFromSlice(HelperEnvelope, allocator, payload, .{}) catch return null;
    defer parsed.deinit();

    const envelope = parsed.value;
    if (envelope.status != .ok or envelope.action == null or envelope.action.? != .capture) return null;
    const local = validEnvelopeGeometry(envelope.local_geometry) orelse return null;
    const output = envelope.output orelse return null;
    if (output.width == 0 or output.height == 0) return null;

    return .{
        .output_name = if (output.name) |name| try allocator.dupe(u8, name) else null,
        .output_origin_x = output.x,
        .output_origin_y = output.y,
        .output_width = output.width,
        .output_height = output.height,
        .geometry = .{
            .x = local.x,
            .y = local.y,
            .width = local.width,
            .height = local.height,
        },
    };
}

pub fn reportsUnavailable(allocator: std.mem.Allocator, payload: []const u8) !bool {
    const parsed = std.json.parseFromSlice(HelperEnvelope, allocator, payload, .{}) catch return false;
    defer parsed.deinit();

    const envelope = parsed.value;
    if (envelope.status != .@"error") return false;

    const helper_error = envelope.@"error" orelse return false;
    return std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_UNAVAILABLE");
}

/// Resolve deterministic overlay helper failure taxonomy for cancelled selections.
pub fn deterministicFailureCode(
    environ: std.process.Environ,
    simulate_cancel: bool,
    selection_cancelled: bool,
) ?[]const u8 {
    if (!selection_cancelled or simulate_cancel) return null;

    if (environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE")) |raw_mode_z| {
        const raw_mode = std.mem.sliceTo(raw_mode_z, 0);
        if (std.mem.eql(u8, raw_mode, "malformed")) return "ERR_OVERLAY_PROTOCOL_INVALID";
        if (std.mem.eql(u8, raw_mode, "timeout")) return "ERR_OVERLAY_TIMEOUT";
        if (std.mem.eql(u8, raw_mode, "unavailable")) return "ERR_OVERLAY_UNAVAILABLE";
    }

    if (envFlagEnabled(environ, "SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT")) {
        return "ERR_OVERLAY_TIMEOUT";
    }
    if (envFlagEnabled(environ, "SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE")) {
        return "ERR_OVERLAY_UNAVAILABLE";
    }
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_BIN")) |_| {
        return "ERR_OVERLAY_UNAVAILABLE";
    }

    return null;
}

pub fn testPayload(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE")) |raw_mode_z| {
        const raw_mode = std.mem.sliceTo(raw_mode_z, 0);
        if (std.mem.eql(u8, raw_mode, "ok")) {
            return "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":320,\"y\":180,\"width\":640,\"height\":360},\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "cancel")) {
            return "{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "malformed")) {
            return "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":\"bad\",\"y\":1,\"width\":2,\"height\":3},\"error\":null}";
        }
        if (std.mem.eql(u8, raw_mode, "timeout") or std.mem.eql(u8, raw_mode, "unavailable")) {
            return "{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_HELPER_TEST\",\"message\":\"forced helper failure\"}}";
        }
    }
    return null;
}

/// Build deterministic helper-envelope payloads from scripted interaction scenarios.
pub fn deterministicInteractionScenarioPayload(
    allocator: std.mem.Allocator,
    environ: std.process.Environ,
    constraint: selection.SelectionConstraint,
) !?[]u8 {
    const raw_mode_z = environ.getPosix("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE") orelse return null;
    const raw_mode = std.mem.sliceTo(raw_mode_z, 0);

    const scenario: selection.DeterministicScenario = blk: {
        if (std.mem.eql(u8, raw_mode, "interaction_drag")) break :blk .drag_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_cancel")) break :blk .drag_cancel_escape;
        if (std.mem.eql(u8, raw_mode, "interaction_resize")) break :blk .drag_then_resize_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_move")) break :blk .drag_then_move_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_edge_resize")) break :blk .drag_then_edge_resize_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_nudge")) break :blk .drag_then_nudge_confirm;
        if (std.mem.eql(u8, raw_mode, "interaction_large_nudge")) break :blk .drag_then_large_nudge_confirm;
        return null;
    };

    const simulated = selection.simulateDeterministicScenario(.freeform, constraint, scenario);
    if (simulated.cancelled) {
        return try std.fmt.allocPrint(
            allocator,
            "{{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}}",
            .{},
        );
    }

    const geometry = simulated.geometry orelse {
        return try std.fmt.allocPrint(
            allocator,
            "{{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}}",
            .{},
        );
    };

    return try std.fmt.allocPrint(
        allocator,
        "{{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}},\"error\":null}}",
        .{ geometry.x, geometry.y, geometry.width, geometry.height },
    );
}

pub fn envFlagEnabled(environ: std.process.Environ, key: []const u8) bool {
    if (environ.getPosix(key)) |raw_z| {
        const raw = std.mem.sliceTo(raw_z, 0);
        return std.mem.eql(u8, raw, "1") or std.ascii.eqlIgnoreCase(raw, "true") or std.ascii.eqlIgnoreCase(raw, "yes");
    }
    return false;
}

fn validEnvelopeGeometry(geometry: ?HelperGeometry) ?HelperGeometry {
    const value = geometry orelse return null;
    if (value.width == 0 or value.height == 0) return null;
    return value;
}

fn resultFromGeometry(
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
    geometry: HelperGeometry,
    cancelled: bool,
) selection.SelectionResult {
    return .{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = .{
            .x = geometry.x,
            .y = geometry.y,
            .width = geometry.width,
            .height = geometry.height,
        },
        .cancelled = cancelled,
    };
}

fn validAspect(aspect: ?[]const u8) ?[]const u8 {
    const raw = aspect orelse return null;
    if (freeAspect(aspect)) return null;
    var parts = std.mem.splitScalar(u8, raw, ':');
    const w_raw = parts.next() orelse return null;
    const h_raw = parts.next() orelse return null;
    if (parts.next() != null) return null;
    const w = std.fmt.parseInt(u32, w_raw, 10) catch return null;
    const h = std.fmt.parseInt(u32, h_raw, 10) catch return null;
    if (w == 0 or h == 0) return null;
    return raw;
}

fn freeAspect(aspect: ?[]const u8) bool {
    const raw = aspect orelse return false;
    return std.ascii.eqlIgnoreCase(raw, "Free");
}

fn cancelledSelection(mode: selection.SelectionMode, constraint: selection.SelectionConstraint) selection.SelectionResult {
    return .{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = null,
        .cancelled = true,
    };
}

test "helper aspect override extracts fixed custom and free values" {
    const custom = try parseAspectOverrideAlloc(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"aspect\":\"16:3\",\"geometry\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4},\"error\":null}",
    );
    defer custom.deinit(std.testing.allocator);
    try std.testing.expect(custom == .value);
    try std.testing.expectEqualStrings("16:3", custom.value);

    const free = try parseAspectOverrideAlloc(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"aspect\":\"Free\",\"geometry\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4},\"error\":null}",
    );
    defer free.deinit(std.testing.allocator);
    try std.testing.expect(free == .free);
}

test "helper aspect override ignores missing or malformed values" {
    const missing = try parseAspectOverrideAlloc(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4},\"error\":null}",
    );
    defer missing.deinit(std.testing.allocator);
    try std.testing.expect(missing == .missing);

    const malformed = try parseAspectOverrideAlloc(
        std.testing.allocator,
        "{\"status\":\"ok\",\"action\":\"capture\",\"aspect\":\"16/9\",\"geometry\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4},\"error\":null}",
    );
    defer malformed.deinit(std.testing.allocator);
    try std.testing.expect(malformed == .missing);
}
