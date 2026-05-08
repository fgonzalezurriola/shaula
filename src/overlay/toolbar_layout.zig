const std = @import("std");

pub const Rect = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

pub const Point = struct {
    x: i32,
    y: i32,
};

pub const Size = struct {
    width: u32,
    height: u32,
};

pub const SelectionRect = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

pub const Placement = enum {
    below,
    above,
    anchored,
};

pub const LayoutPolicy = struct {
    padding: i32 = 12,
    handle_clearance: i32 = 18,
    badge_clearance: i32 = 30,
    jitter_threshold: i32 = 6,
};

pub const LayoutResult = struct {
    position: Point,
    placement: Placement,
};

/// Computes the floating capture toolbar position from visible output bounds.
///
/// Contract constraints:
/// - prefer below selection, then above, then nearest visible edge with padding.
/// - clamp to output bounds so the toolbar never leaves the visible output.
/// - preserve the previous valid position for tiny movements to avoid jitter.
pub fn compute(
    output: Rect,
    selection_rect: SelectionRect,
    toolbar: Size,
    badge: ?Rect,
    previous: ?Point,
    policy: LayoutPolicy,
) LayoutResult {
    const selected = rectFromSelection(selection_rect);
    const toolbar_w: i32 = @intCast(toolbar.width);
    const toolbar_h: i32 = @intCast(toolbar.height);

    const min_x = output.x + policy.padding;
    const max_x = output.x + @as(i32, @intCast(output.width)) - policy.padding - toolbar_w;
    const min_y = output.y + policy.padding;
    const max_y = output.y + @as(i32, @intCast(output.height)) - policy.padding - toolbar_h;

    const centered_x = selected.x + @divFloor(@as(i32, @intCast(selected.width)) - toolbar_w, 2);
    const below_y = selected.y + @as(i32, @intCast(selected.height)) + policy.handle_clearance;
    const above_y = selected.y - toolbar_h - policy.handle_clearance - policy.badge_clearance;

    var candidate: LayoutResult = if (below_y <= max_y)
        .{ .position = .{ .x = clamp(centered_x, min_x, max_x), .y = below_y }, .placement = .below }
    else if (above_y >= min_y)
        .{ .position = .{ .x = clamp(centered_x, min_x, max_x), .y = above_y }, .placement = .above }
    else
        anchored(output, selected, toolbar, policy);

    candidate.position.x = clamp(candidate.position.x, min_x, max_x);
    candidate.position.y = clamp(candidate.position.y, min_y, max_y);

    if (badge) |badge_rect| {
        if (overlaps(rectAt(candidate.position, toolbar), badge_rect)) {
            const alternate_y = selected.y - toolbar_h - policy.handle_clearance - policy.badge_clearance;
            if (alternate_y >= min_y) {
                candidate.position.y = alternate_y;
                candidate.placement = .above;
            }
        }
    }

    if (previous) |last| {
        if (distanceWithin(last, candidate.position, policy.jitter_threshold)) {
            candidate.position = last;
        }
    }

    return candidate;
}

fn rectFromSelection(geometry: SelectionRect) Rect {
    return .{ .x = geometry.x, .y = geometry.y, .width = geometry.width, .height = geometry.height };
}

fn rectAt(point: Point, size: Size) Rect {
    return .{ .x = point.x, .y = point.y, .width = size.width, .height = size.height };
}

fn anchored(output: Rect, selected: Rect, toolbar: Size, policy: LayoutPolicy) LayoutResult {
    const toolbar_w: i32 = @intCast(toolbar.width);
    const toolbar_h: i32 = @intCast(toolbar.height);
    const output_bottom = output.y + @as(i32, @intCast(output.height));
    const room_below = output_bottom - (selected.y + @as(i32, @intCast(selected.height)));
    const room_above = selected.y - output.y;

    const y = if (room_below >= room_above)
        output_bottom - policy.padding - toolbar_h
    else
        output.y + policy.padding;

    return .{
        .position = .{
            .x = output.x + @divFloor(@as(i32, @intCast(output.width)) - toolbar_w, 2),
            .y = y,
        },
        .placement = .anchored,
    };
}

fn overlaps(a: Rect, b: Rect) bool {
    const ar = a.x + @as(i32, @intCast(a.width));
    const ab = a.y + @as(i32, @intCast(a.height));
    const br = b.x + @as(i32, @intCast(b.width));
    const bb = b.y + @as(i32, @intCast(b.height));
    return a.x < br and ar > b.x and a.y < bb and ab > b.y;
}

fn clamp(value: i32, min_value: i32, max_value: i32) i32 {
    if (max_value < min_value) return min_value;
    return @max(min_value, @min(value, max_value));
}

fn distanceWithin(a: Point, b: Point, threshold: i32) bool {
    return @abs(a.x - b.x) <= threshold and @abs(a.y - b.y) <= threshold;
}
