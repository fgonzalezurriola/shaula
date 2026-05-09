const std = @import("std");

const CInt = c_int;
const TRUE: CInt = 1;
const FALSE: CInt = 0;

const ShaulaPoint = extern struct {
    x: f64,
    y: f64,
};

const ShaulaRect = extern struct {
    x: f64,
    y: f64,
    width: f64,
    height: f64,
};

const ShaulaColor = extern struct {
    r: f64,
    g: f64,
    b: f64,
    a: f64,
};

export fn shaula_color_default() ShaulaColor {
    return .{ .r = 0.165, .g = 0.290, .b = 0.400, .a = 1.0 };
}

export fn shaula_color_to_hex(color: ShaulaColor, out: [*]u8) void {
    const r = colorChannel(color.r);
    const g = colorChannel(color.g);
    const b = colorChannel(color.b);
    _ = std.fmt.bufPrint(out[0..8], "#{X:0>2}{X:0>2}{X:0>2}", .{ r, g, b }) catch {};
}

export fn shaula_rect_from_points(a: ShaulaPoint, b: ShaulaPoint) ShaulaRect {
    return shaula_rect_normalized(.{ .x = a.x, .y = a.y, .width = b.x - a.x, .height = b.y - a.y });
}

export fn shaula_rect_normalized(input: ShaulaRect) ShaulaRect {
    var rect = input;
    if (rect.width < 0) {
        rect.x += rect.width;
        rect.width = -rect.width;
    }
    if (rect.height < 0) {
        rect.y += rect.height;
        rect.height = -rect.height;
    }
    return rect;
}

export fn shaula_rect_clamped(input: ShaulaRect, max_width: f64, max_height: f64) ShaulaRect {
    const rect = shaula_rect_normalized(input);
    const x1 = std.math.clamp(rect.x, 0.0, max_width);
    const y1 = std.math.clamp(rect.y, 0.0, max_height);
    const x2 = std.math.clamp(rect.x + rect.width, 0.0, max_width);
    const y2 = std.math.clamp(rect.y + rect.height, 0.0, max_height);
    return shaula_rect_from_points(.{ .x = x1, .y = y1 }, .{ .x = x2, .y = y2 });
}

export fn shaula_rect_expanded(input: ShaulaRect, amount: f64) ShaulaRect {
    const rect = shaula_rect_normalized(input);
    return .{ .x = rect.x - amount, .y = rect.y - amount, .width = rect.width + amount * 2.0, .height = rect.height + amount * 2.0 };
}

export fn shaula_rect_union(a_input: ShaulaRect, b_input: ShaulaRect) ShaulaRect {
    const a = shaula_rect_normalized(a_input);
    const b = shaula_rect_normalized(b_input);
    const x1 = @min(a.x, b.x);
    const y1 = @min(a.y, b.y);
    const x2 = @max(a.x + a.width, b.x + b.width);
    const y2 = @max(a.y + a.height, b.y + b.height);
    return .{ .x = x1, .y = y1, .width = x2 - x1, .height = y2 - y1 };
}

export fn shaula_rect_is_empty(input: ShaulaRect) CInt {
    const rect = shaula_rect_normalized(input);
    return if (rect.width <= 0.5 or rect.height <= 0.5) TRUE else FALSE;
}

export fn shaula_rect_contains_point(input: ShaulaRect, point: ShaulaPoint) CInt {
    const rect = shaula_rect_normalized(input);
    const contains = point.x >= rect.x and point.y >= rect.y and point.x <= rect.x + rect.width and point.y <= rect.y + rect.height;
    return if (contains) TRUE else FALSE;
}

export fn shaula_rect_intersects(a_input: ShaulaRect, b_input: ShaulaRect) CInt {
    const a = shaula_rect_normalized(a_input);
    const b = shaula_rect_normalized(b_input);
    const intersects = a.x <= b.x + b.width and a.x + a.width >= b.x and a.y <= b.y + b.height and a.y + a.height >= b.y;
    return if (intersects) TRUE else FALSE;
}

export fn shaula_point_distance(a: ShaulaPoint, b: ShaulaPoint) f64 {
    const dx = a.x - b.x;
    const dy = a.y - b.y;
    return @sqrt(dx * dx + dy * dy);
}

export fn shaula_point_distance_to_segment(point: ShaulaPoint, a: ShaulaPoint, b: ShaulaPoint) f64 {
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const len2 = dx * dx + dy * dy;
    if (len2 <= 0.000001) return shaula_point_distance(point, a);
    const t = std.math.clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / len2, 0.0, 1.0);
    const projection = ShaulaPoint{ .x = a.x + t * dx, .y = a.y + t * dy };
    return shaula_point_distance(point, projection);
}

export fn shaula_point_clamped(point_input: ShaulaPoint, max_width: f64, max_height: f64) ShaulaPoint {
    return .{
        .x = std.math.clamp(point_input.x, 0.0, max_width),
        .y = std.math.clamp(point_input.y, 0.0, max_height),
    };
}

fn colorChannel(value: f64) u8 {
    return @intFromFloat(std.math.clamp(value, 0.0, 1.0) * 255.0 + 0.5);
}
