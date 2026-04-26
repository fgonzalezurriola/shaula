const std = @import("std");

pub const SelectionMode = enum {
    freeform,
    window,
    output,
};

pub const SelectionConstraint = struct {
    aspect: ?[]const u8 = null,
};

pub const Geometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

pub const SelectionResult = struct {
    mode: SelectionMode,
    aspect: ?[]const u8,
    geometry: ?Geometry,
    cancelled: bool,
};

pub const Pointer = struct {
    x: i32,
    y: i32,
};

pub const ResizeHandle = enum {
    top_left,
    top_right,
    bottom_left,
    bottom_right,
};

pub const InteractionPhase = enum {
    idle,
    drawing,
    resizing,
    ready,
    confirmed,
    cancelled,
};

pub const InteractionEvent = union(enum) {
    pointer_down: Pointer,
    pointer_move: Pointer,
    pointer_up,
    begin_resize: ResizeHandle,
    press_enter,
    press_escape,
    click_confirm,
    click_cancel,
};

/// Deterministic state machine for helper-side selection interactions.
///
/// Contract constraints:
/// - drag and resize transitions must never emit zero-sized geometry on confirm.
/// - Esc always produces `cancelled=true`.
/// - aspect constraints are enforced at geometry computation boundaries so emitted
///   rectangles remain stable in CI/headless simulation.
pub const InteractionState = struct {
    phase: InteractionPhase = .idle,
    constraint: SelectionConstraint,
    anchor: Pointer = .{ .x = 0, .y = 0 },
    pointer: Pointer = .{ .x = 0, .y = 0 },
    geometry: ?Geometry = null,
    active_handle: ?ResizeHandle = null,

    pub fn init(constraint: SelectionConstraint) InteractionState {
        return .{
            .constraint = constraint,
        };
    }

    pub fn apply(self: *InteractionState, event: InteractionEvent) void {
        switch (event) {
            .pointer_down => |point| {
                self.anchor = point;
                self.pointer = point;
                self.geometry = null;
                self.active_handle = null;
                self.phase = .drawing;
            },
            .pointer_move => |point| {
                self.pointer = point;
                switch (self.phase) {
                    .drawing => {
                        self.geometry = geometryFromPoints(self.anchor, point, self.constraint.aspect);
                    },
                    .resizing => {
                        const current = self.geometry orelse return;
                        const handle = self.active_handle orelse return;
                        const opposite = oppositeCorner(current, handle);
                        self.geometry = geometryFromPoints(opposite, point, self.constraint.aspect);
                    },
                    else => {},
                }
            },
            .pointer_up => {
                if ((self.phase == .drawing or self.phase == .resizing) and isNonZero(self.geometry)) {
                    self.phase = .ready;
                    return;
                }

                if (self.phase == .drawing or self.phase == .resizing) {
                    self.phase = .idle;
                    self.geometry = null;
                    self.active_handle = null;
                }
            },
            .begin_resize => |handle| {
                if (self.phase == .ready and isNonZero(self.geometry)) {
                    self.phase = .resizing;
                    self.active_handle = handle;
                }
            },
            .press_enter => {
                if (isNonZero(self.geometry)) {
                    self.phase = .confirmed;
                } else {
                    self.phase = .cancelled;
                    self.geometry = null;
                }
            },
            .press_escape => {
                self.phase = .cancelled;
                self.geometry = null;
            },
            .click_confirm => {
                if (isNonZero(self.geometry)) {
                    self.phase = .confirmed;
                } else {
                    self.phase = .cancelled;
                    self.geometry = null;
                }
            },
            .click_cancel => {
                self.phase = .cancelled;
                self.geometry = null;
            },
        }
    }

    pub fn result(self: InteractionState, mode: SelectionMode) SelectionResult {
        if (self.phase == .cancelled) {
            return .{
                .mode = mode,
                .aspect = self.constraint.aspect,
                .geometry = null,
                .cancelled = true,
            };
        }

        if (isNonZero(self.geometry) and (self.phase == .ready or self.phase == .confirmed)) {
            return .{
                .mode = mode,
                .aspect = self.constraint.aspect,
                .geometry = self.geometry,
                .cancelled = false,
            };
        }

        return .{
            .mode = mode,
            .aspect = self.constraint.aspect,
            .geometry = null,
            .cancelled = true,
        };
    }
};

pub const DeterministicScenario = enum {
    drag_confirm,
    drag_cancel_escape,
    drag_then_resize_confirm,
};

/// Runs deterministic scripted interactions used by helper/test simulation lanes.
pub fn simulateDeterministicScenario(
    mode: SelectionMode,
    constraint: SelectionConstraint,
    scenario: DeterministicScenario,
) SelectionResult {
    var state = InteractionState.init(constraint);

    switch (scenario) {
        .drag_confirm => {
            state.apply(.{ .pointer_down = .{ .x = 100, .y = 100 } });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.pointer_up);
            state.apply(.press_enter);
        },
        .drag_cancel_escape => {
            state.apply(.{ .pointer_down = .{ .x = 100, .y = 100 } });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.press_escape);
        },
        .drag_then_resize_confirm => {
            state.apply(.{ .pointer_down = .{ .x = 100, .y = 100 } });
            state.apply(.{ .pointer_move = .{ .x = 420, .y = 300 } });
            state.apply(.pointer_up);
            state.apply(.{ .begin_resize = .bottom_right });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.pointer_up);
            state.apply(.press_enter);
        },
    }

    return state.result(mode);
}

const AspectRatio = struct {
    width: u32,
    height: u32,
};

fn geometryFromPoints(anchor: Pointer, point: Pointer, aspect: ?[]const u8) ?Geometry {
    var abs_w: i64 = @intCast(@abs(point.x - anchor.x));
    var abs_h: i64 = @intCast(@abs(point.y - anchor.y));

    if (parseAspectRatio(aspect)) |ratio| {
        if (abs_w == 0 and abs_h > 0) {
            abs_w = @max(1, @divFloor(abs_h * @as(i64, ratio.width), @as(i64, ratio.height)));
        } else if (abs_h == 0 and abs_w > 0) {
            abs_h = @max(1, @divFloor(abs_w * @as(i64, ratio.height), @as(i64, ratio.width)));
        } else if (abs_w > 0 and abs_h > 0) {
            const lhs = abs_w * @as(i64, ratio.height);
            const rhs = abs_h * @as(i64, ratio.width);
            if (lhs >= rhs) {
                abs_w = @max(1, @divFloor(abs_h * @as(i64, ratio.width), @as(i64, ratio.height)));
            } else {
                abs_h = @max(1, @divFloor(abs_w * @as(i64, ratio.height), @as(i64, ratio.width)));
            }
        }
    }

    if (abs_w <= 0 or abs_h <= 0) return null;

    const width: u32 = @intCast(abs_w);
    const height: u32 = @intCast(abs_h);

    const x = if (point.x >= anchor.x) anchor.x else anchor.x - @as(i32, @intCast(width));
    const y = if (point.y >= anchor.y) anchor.y else anchor.y - @as(i32, @intCast(height));

    return .{
        .x = x,
        .y = y,
        .width = width,
        .height = height,
    };
}

fn oppositeCorner(geometry: Geometry, handle: ResizeHandle) Pointer {
    const right = geometry.x + @as(i32, @intCast(geometry.width));
    const bottom = geometry.y + @as(i32, @intCast(geometry.height));

    return switch (handle) {
        .top_left => .{ .x = right, .y = bottom },
        .top_right => .{ .x = geometry.x, .y = bottom },
        .bottom_left => .{ .x = right, .y = geometry.y },
        .bottom_right => .{ .x = geometry.x, .y = geometry.y },
    };
}

fn parseAspectRatio(aspect: ?[]const u8) ?AspectRatio {
    const raw = aspect orelse return null;
    var parts = std.mem.splitScalar(u8, raw, ':');
    const left = parts.next() orelse return null;
    const right = parts.next() orelse return null;
    if (parts.next() != null) return null;

    const width = std.fmt.parseInt(u32, left, 10) catch return null;
    const height = std.fmt.parseInt(u32, right, 10) catch return null;
    if (width == 0 or height == 0) return null;

    return .{ .width = width, .height = height };
}

fn isNonZero(geometry: ?Geometry) bool {
    if (geometry) |g| return g.width > 0 and g.height > 0;
    return false;
}

test "deterministic drag emits non-zero geometry" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(u32, 400), geometry.width);
    try std.testing.expectEqual(@as(u32, 300), geometry.height);
}

test "deterministic escape path cancels selection" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_cancel_escape);
    try std.testing.expect(result.cancelled);
    try std.testing.expect(result.geometry == null);
}

test "drag respects 16:9 aspect constraints" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = "16:9" }, .drag_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(u32, 400), geometry.width);
    try std.testing.expectEqual(@as(u32, 225), geometry.height);
}

test "resize handle keeps constraints during confirm" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = "4:3" }, .drag_then_resize_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expect(geometry.width > 0 and geometry.height > 0);
    try std.testing.expectEqual(@as(u32, @divFloor(geometry.width * 3, 4)), geometry.height);
}
