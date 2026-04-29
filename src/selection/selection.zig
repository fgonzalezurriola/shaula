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

pub const OutputBounds = struct {
    x: i32 = 0,
    y: i32 = 0,
    width: u32 = 800,
    height: u32 = 600,
};

pub const ResizeHandle = enum {
    top_left,
    top,
    top_right,
    right,
    bottom_right,
    bottom,
    bottom_left,
    left,
};

pub const Nudge = struct {
    dx: i32,
    dy: i32,
};

pub const InteractionPhase = enum {
    idle,
    drawing,
    moving,
    resizing,
    ready,
    confirmed,
    cancelled,
};

pub const InteractionEvent = union(enum) {
    pointer_down: Pointer,
    pointer_move: Pointer,
    pointer_up,
    begin_move: Pointer,
    begin_resize: ResizeHandle,
    nudge: Nudge,
    nudge_large: Nudge,
    press_enter,
    press_escape,
    click_confirm,
    click_cancel,
};

/// Deterministic state machine for helper-side selection interactions.
///
/// Contract constraints:
/// - drag, move, resize, and nudge transitions never emit zero-sized geometry on confirm.
/// - Esc always produces `cancelled=true`.
/// - aspect constraints are enforced at geometry computation boundaries so emitted
///   rectangles remain stable in CI/headless simulation.
/// - geometry is clamped to the visible output contract before becoming observable.
pub const InteractionState = struct {
    phase: InteractionPhase = .idle,
    constraint: SelectionConstraint,
    output: OutputBounds = .{},
    anchor: Pointer = .{ .x = 0, .y = 0 },
    pointer: Pointer = .{ .x = 0, .y = 0 },
    geometry: ?Geometry = null,
    active_handle: ?ResizeHandle = null,
    move_origin: Pointer = .{ .x = 0, .y = 0 },
    move_start: ?Geometry = null,

    pub fn init(constraint: SelectionConstraint) InteractionState {
        return .{
            .constraint = constraint,
        };
    }

    pub fn initWithOutput(constraint: SelectionConstraint, output: OutputBounds) InteractionState {
        return .{
            .constraint = constraint,
            .output = output,
        };
    }

    pub fn apply(self: *InteractionState, event: InteractionEvent) void {
        switch (event) {
            .pointer_down => |point| {
                self.anchor = point;
                self.pointer = point;
                self.geometry = null;
                self.active_handle = null;
                self.move_start = null;
                self.phase = .drawing;
            },
            .pointer_move => |point| {
                self.pointer = point;
                switch (self.phase) {
                    .drawing => {
                        self.geometry = clampGeometry(
                            geometryFromPoints(self.anchor, point, self.constraint.aspect),
                            self.output,
                        );
                    },
                    .moving => {
                        const start = self.move_start orelse return;
                        self.geometry = moveGeometry(start, point.x - self.move_origin.x, point.y - self.move_origin.y, self.output);
                    },
                    .resizing => {
                        const current = self.geometry orelse return;
                        const handle = self.active_handle orelse return;
                        self.geometry = resizeGeometry(current, handle, point, self.constraint.aspect, self.output);
                    },
                    else => {},
                }
            },
            .pointer_up => {
                if ((self.phase == .drawing or self.phase == .moving or self.phase == .resizing) and isNonZero(self.geometry)) {
                    self.phase = .ready;
                    self.active_handle = null;
                    self.move_start = null;
                    return;
                }

                if (self.phase == .drawing or self.phase == .moving or self.phase == .resizing) {
                    self.phase = .idle;
                    self.geometry = null;
                    self.active_handle = null;
                    self.move_start = null;
                }
            },
            .begin_move => |point| {
                if (self.phase == .ready and isNonZero(self.geometry)) {
                    self.phase = .moving;
                    self.move_origin = point;
                    self.move_start = self.geometry;
                    self.active_handle = null;
                }
            },
            .begin_resize => |handle| {
                if (self.phase == .ready and isNonZero(self.geometry)) {
                    self.phase = .resizing;
                    self.active_handle = handle;
                    self.move_start = null;
                }
            },
            .nudge => |delta| {
                if (self.phase == .ready and isNonZero(self.geometry)) {
                    self.geometry = moveGeometry(self.geometry.?, delta.dx, delta.dy, self.output);
                }
            },
            .nudge_large => |delta| {
                if (self.phase == .ready and isNonZero(self.geometry)) {
                    self.geometry = moveGeometry(self.geometry.?, delta.dx * 10, delta.dy * 10, self.output);
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
                self.active_handle = null;
                self.move_start = null;
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
                self.active_handle = null;
                self.move_start = null;
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
    drag_then_move_confirm,
    drag_then_edge_resize_confirm,
    drag_then_nudge_confirm,
    drag_then_large_nudge_confirm,
    drag_aspect_confirm,
    move_cancel_escape,
    resize_cancel_escape,
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
            dragBase(&state, .{ .x = 500, .y = 400 });
            state.apply(.press_enter);
        },
        .drag_cancel_escape => {
            state.apply(.{ .pointer_down = .{ .x = 100, .y = 100 } });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.press_escape);
        },
        .drag_then_resize_confirm => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .begin_resize = .bottom_right });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.pointer_up);
            state.apply(.press_enter);
        },
        .drag_then_move_confirm => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .begin_move = .{ .x = 160, .y = 140 } });
            state.apply(.{ .pointer_move = .{ .x = 220, .y = 180 } });
            state.apply(.pointer_up);
            state.apply(.press_enter);
        },
        .drag_then_edge_resize_confirm => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .begin_resize = .right });
            state.apply(.{ .pointer_move = .{ .x = 520, .y = 210 } });
            state.apply(.pointer_up);
            state.apply(.press_enter);
        },
        .drag_then_nudge_confirm => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .nudge = .{ .dx = 1, .dy = -1 } });
            state.apply(.press_enter);
        },
        .drag_then_large_nudge_confirm => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .nudge_large = .{ .dx = 1, .dy = 1 } });
            state.apply(.press_enter);
        },
        .drag_aspect_confirm => {
            dragBase(&state, .{ .x = 500, .y = 400 });
            state.apply(.press_enter);
        },
        .move_cancel_escape => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .begin_move = .{ .x = 160, .y = 140 } });
            state.apply(.{ .pointer_move = .{ .x = 220, .y = 180 } });
            state.apply(.press_escape);
        },
        .resize_cancel_escape => {
            dragBase(&state, .{ .x = 420, .y = 300 });
            state.apply(.{ .begin_resize = .bottom_right });
            state.apply(.{ .pointer_move = .{ .x = 500, .y = 400 } });
            state.apply(.press_escape);
        },
    }

    return state.result(mode);
}

const AspectRatio = struct {
    width: u32,
    height: u32,
};

fn dragBase(state: *InteractionState, point: Pointer) void {
    state.apply(.{ .pointer_down = .{ .x = 100, .y = 100 } });
    state.apply(.{ .pointer_move = point });
    state.apply(.pointer_up);
}

fn geometryFromPoints(anchor: Pointer, point: Pointer, aspect: ?[]const u8) ?Geometry {
    var abs_w: i64 = @intCast(@abs(point.x - anchor.x));
    var abs_h: i64 = @intCast(@abs(point.y - anchor.y));

    if (parseAspectRatio(aspect)) |ratio| {
        applyAspectToSize(&abs_w, &abs_h, ratio);
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

fn resizeGeometry(
    current: Geometry,
    handle: ResizeHandle,
    point: Pointer,
    aspect: ?[]const u8,
    output: OutputBounds,
) ?Geometry {
    const ratio = parseAspectRatio(aspect);
    if (ratio) |r| {
        return clampGeometry(resizeWithAspect(current, handle, point, r), output);
    }

    const right = current.x + @as(i32, @intCast(current.width));
    const bottom = current.y + @as(i32, @intCast(current.height));
    var left = current.x;
    var top = current.y;
    var new_right = right;
    var new_bottom = bottom;

    switch (handle) {
        .top_left => {
            left = point.x;
            top = point.y;
        },
        .top => top = point.y,
        .top_right => {
            new_right = point.x;
            top = point.y;
        },
        .right => new_right = point.x,
        .bottom_right => {
            new_right = point.x;
            new_bottom = point.y;
        },
        .bottom => new_bottom = point.y,
        .bottom_left => {
            left = point.x;
            new_bottom = point.y;
        },
        .left => left = point.x,
    }

    return clampGeometry(geometryFromEdges(left, top, new_right, new_bottom), output);
}

fn resizeWithAspect(current: Geometry, handle: ResizeHandle, point: Pointer, ratio: AspectRatio) ?Geometry {
    const right = current.x + @as(i32, @intCast(current.width));
    const bottom = current.y + @as(i32, @intCast(current.height));
    const center_x = current.x + @divFloor(@as(i32, @intCast(current.width)), 2);
    const center_y = current.y + @divFloor(@as(i32, @intCast(current.height)), 2);

    return switch (handle) {
        .top_left => geometryFromPointsWithRatio(.{ .x = right, .y = bottom }, point, ratio),
        .top_right => geometryFromPointsWithRatio(.{ .x = current.x, .y = bottom }, point, ratio),
        .bottom_left => geometryFromPointsWithRatio(.{ .x = right, .y = current.y }, point, ratio),
        .bottom_right => geometryFromPointsWithRatio(.{ .x = current.x, .y = current.y }, point, ratio),
        .top => resizeVerticalAspect(bottom, point.y, center_x, ratio),
        .bottom => resizeVerticalAspect(current.y, point.y, center_x, ratio),
        .left => resizeHorizontalAspect(right, point.x, center_y, ratio),
        .right => resizeHorizontalAspect(current.x, point.x, center_y, ratio),
    };
}

fn geometryFromPointsWithRatio(anchor: Pointer, point: Pointer, ratio: AspectRatio) ?Geometry {
    var abs_w: i64 = @intCast(@abs(point.x - anchor.x));
    var abs_h: i64 = @intCast(@abs(point.y - anchor.y));
    applyAspectToSize(&abs_w, &abs_h, ratio);
    if (abs_w <= 0 or abs_h <= 0) return null;

    const width: u32 = @intCast(abs_w);
    const height: u32 = @intCast(abs_h);
    return .{
        .x = if (point.x >= anchor.x) anchor.x else anchor.x - @as(i32, @intCast(width)),
        .y = if (point.y >= anchor.y) anchor.y else anchor.y - @as(i32, @intCast(height)),
        .width = width,
        .height = height,
    };
}

fn resizeVerticalAspect(fixed_y: i32, moving_y: i32, center_x: i32, ratio: AspectRatio) ?Geometry {
    const height_i: i64 = @intCast(@abs(moving_y - fixed_y));
    if (height_i <= 0) return null;
    const width_i = @max(1, @divFloor(height_i * @as(i64, ratio.width), @as(i64, ratio.height)));
    const width: u32 = @intCast(width_i);
    const height: u32 = @intCast(height_i);
    return .{
        .x = center_x - @divFloor(@as(i32, @intCast(width)), 2),
        .y = @min(fixed_y, moving_y),
        .width = width,
        .height = height,
    };
}

fn resizeHorizontalAspect(fixed_x: i32, moving_x: i32, center_y: i32, ratio: AspectRatio) ?Geometry {
    const width_i: i64 = @intCast(@abs(moving_x - fixed_x));
    if (width_i <= 0) return null;
    const height_i = @max(1, @divFloor(width_i * @as(i64, ratio.height), @as(i64, ratio.width)));
    const width: u32 = @intCast(width_i);
    const height: u32 = @intCast(height_i);
    return .{
        .x = @min(fixed_x, moving_x),
        .y = center_y - @divFloor(@as(i32, @intCast(height)), 2),
        .width = width,
        .height = height,
    };
}

fn geometryFromEdges(left: i32, top: i32, right: i32, bottom: i32) ?Geometry {
    const x = @min(left, right);
    const y = @min(top, bottom);
    const width_i = @abs(right - left);
    const height_i = @abs(bottom - top);
    if (width_i == 0 or height_i == 0) return null;
    return .{
        .x = x,
        .y = y,
        .width = @intCast(width_i),
        .height = @intCast(height_i),
    };
}

fn moveGeometry(geometry: Geometry, dx: i32, dy: i32, output: OutputBounds) ?Geometry {
    const max_x = output.x + @as(i32, @intCast(output.width)) - @as(i32, @intCast(geometry.width));
    const max_y = output.y + @as(i32, @intCast(output.height)) - @as(i32, @intCast(geometry.height));
    return .{
        .x = clamp(geometry.x + dx, output.x, max_x),
        .y = clamp(geometry.y + dy, output.y, max_y),
        .width = geometry.width,
        .height = geometry.height,
    };
}

fn clampGeometry(geometry: ?Geometry, output: OutputBounds) ?Geometry {
    const g = geometry orelse return null;
    if (g.width == 0 or g.height == 0) return null;

    const output_right = output.x + @as(i32, @intCast(output.width));
    const output_bottom = output.y + @as(i32, @intCast(output.height));
    const left = clamp(g.x, output.x, output_right - 1);
    const top = clamp(g.y, output.y, output_bottom - 1);
    var right = clamp(g.x + @as(i32, @intCast(g.width)), output.x + 1, output_right);
    var bottom = clamp(g.y + @as(i32, @intCast(g.height)), output.y + 1, output_bottom);

    if (right <= left) right = @min(output_right, left + 1);
    if (bottom <= top) bottom = @min(output_bottom, top + 1);
    if (right <= left or bottom <= top) return null;

    return .{
        .x = left,
        .y = top,
        .width = @intCast(right - left),
        .height = @intCast(bottom - top),
    };
}

fn applyAspectToSize(abs_w: *i64, abs_h: *i64, ratio: AspectRatio) void {
    if (abs_w.* == 0 and abs_h.* > 0) {
        abs_w.* = @max(1, @divFloor(abs_h.* * @as(i64, ratio.width), @as(i64, ratio.height)));
    } else if (abs_h.* == 0 and abs_w.* > 0) {
        abs_h.* = @max(1, @divFloor(abs_w.* * @as(i64, ratio.height), @as(i64, ratio.width)));
    } else if (abs_w.* > 0 and abs_h.* > 0) {
        const lhs = abs_w.* * @as(i64, ratio.height);
        const rhs = abs_h.* * @as(i64, ratio.width);
        if (lhs >= rhs) {
            abs_w.* = @max(1, @divFloor(abs_h.* * @as(i64, ratio.width), @as(i64, ratio.height)));
        } else {
            abs_h.* = @max(1, @divFloor(abs_w.* * @as(i64, ratio.height), @as(i64, ratio.width)));
        }
    }
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

fn clamp(value: i32, min_value: i32, max_value: i32) i32 {
    if (max_value < min_value) return min_value;
    return @max(min_value, @min(value, max_value));
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

test "move keeps dimensions and changes position" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_then_move_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 160), geometry.x);
    try std.testing.expectEqual(@as(i32, 140), geometry.y);
    try std.testing.expectEqual(@as(u32, 320), geometry.width);
    try std.testing.expectEqual(@as(u32, 200), geometry.height);
}

test "edge resize changes one dimension without aspect lock" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_then_edge_resize_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 100), geometry.x);
    try std.testing.expectEqual(@as(u32, 420), geometry.width);
    try std.testing.expectEqual(@as(u32, 200), geometry.height);
}

test "keyboard nudge moves by one pixel" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_then_nudge_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 101), geometry.x);
    try std.testing.expectEqual(@as(i32, 99), geometry.y);
}

test "large keyboard nudge moves by ten pixels" {
    const result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .drag_then_large_nudge_confirm);
    try std.testing.expect(!result.cancelled);
    const geometry = result.geometry orelse return error.TestExpectedEqual;
    try std.testing.expectEqual(@as(i32, 110), geometry.x);
    try std.testing.expectEqual(@as(i32, 110), geometry.y);
}

test "escape cancels move and resize phases" {
    const move_result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .move_cancel_escape);
    try std.testing.expect(move_result.cancelled);
    try std.testing.expect(move_result.geometry == null);

    const resize_result = simulateDeterministicScenario(.freeform, .{ .aspect = null }, .resize_cancel_escape);
    try std.testing.expect(resize_result.cancelled);
    try std.testing.expect(resize_result.geometry == null);
}
