const std = @import("std");

const selection = @import("../selection/selection.zig");

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

const HelperError = struct {
    code: []const u8,
    message: []const u8,
};

const HelperEnvelope = struct {
    status: HelperStatus,
    geometry: ?HelperGeometry = null,
    action: ?HelperAction = null,
    @"error": ?HelperError = null,
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

pub fn reportsUnavailable(allocator: std.mem.Allocator, payload: []const u8) !bool {
    const parsed = std.json.parseFromSlice(HelperEnvelope, allocator, payload, .{}) catch return false;
    defer parsed.deinit();

    const envelope = parsed.value;
    if (envelope.status != .@"error") return false;

    const helper_error = envelope.@"error" orelse return false;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_UNAVAILABLE")) return true;
    if (std.mem.eql(u8, helper_error.code, "ERR_OVERLAY_TIMEOUT")) return true;
    return false;
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

fn cancelledSelection(mode: selection.SelectionMode, constraint: selection.SelectionConstraint) selection.SelectionResult {
    return .{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = null,
        .cancelled = true,
    };
}
