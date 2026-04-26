const std = @import("std");
const raylib = @import("raylib");
const all_in_one_session = @import("all_in_one_session.zig");
const toolbar_layout = @import("toolbar_layout.zig");

/// HUD State encapsulates the current aspect constraint and UI selections.
pub const HudState = struct {
    aspect: ?[]const u8 = null,
    session: all_in_one_session.AllInOneSession,

    pub fn init(session: all_in_one_session.AllInOneSession) HudState {
        return .{ .session = session };
    }
};

/// Renders the HUD layout using Raylib primitives.
///
/// Contract constraints:
/// - Rendering MUST NOT block selection event loops.
/// - The HUD only exposes the current supported surface: area selection,
///   aspect display, and commit/cancel actions.
/// - Aspect text must reflect the active constraint without mutating layout
///   ownership outside the helper runtime.
pub fn render(state: *HudState) void {
    const aspect_label = state.aspect orelse "Free";
    const output = state.session.output;
    const selection_rect = state.session.selection_rect orelse .{ .x = 100, .y = 100, .width = 400, .height = 300 };
    const toolbar_position = state.session.toolbar.position orelse .{ .x = 244, .y = 418 };
    const toolbar_size = state.session.toolbar.size;
    const screen_width: f32 = @floatFromInt(output.width);
    const screen_height: f32 = @floatFromInt(output.height);
    const sel_x: f32 = @floatFromInt(selection_rect.x);
    const sel_y: f32 = @floatFromInt(selection_rect.y);
    const sel_w: f32 = @floatFromInt(selection_rect.width);
    const sel_h: f32 = @floatFromInt(selection_rect.height);

    // 1. Dim layer (entire screen slightly dimmed)
    raylib.DrawRectangle(0, 0, @as(i32, @intFromFloat(screen_width)), @as(i32, @intFromFloat(screen_height)), raylib.Fade(raylib.BLACK, 0.5));

    // 2. Selection frame
    raylib.DrawRectangleLines(@as(i32, @intFromFloat(sel_x)), @as(i32, @intFromFloat(sel_y)), @as(i32, @intFromFloat(sel_w)), @as(i32, @intFromFloat(sel_h)), raylib.WHITE);
    drawHandles(selection_rect);

    // 3. Live size badge
    var badge_buffer: [32]u8 = undefined;
    const badge = std.fmt.bufPrintZ(&badge_buffer, "{d} x {d}", .{ selection_rect.width, selection_rect.height }) catch "area";
    raylib.DrawRectangle(selection_rect.x, selection_rect.y - 30, 98, 24, raylib.Fade(raylib.BLACK, 0.72));
    raylib.DrawText(badge, selection_rect.x + 8, selection_rect.y - 24, 14, raylib.WHITE);

    drawToolbar(toolbar_position, toolbar_size, aspect_label);
}

fn aspectLabel(aspect: []const u8) []const u8 {
    if (std.mem.eql(u8, aspect, "Free")) return "Aspect: Free";
    return aspect;
}

fn drawHandles(rect: toolbar_layout.SelectionRect) void {
    const size = 10;
    const right = rect.x + @as(i32, @intCast(rect.width));
    const bottom = rect.y + @as(i32, @intCast(rect.height));
    raylib.DrawRectangle(rect.x - 5, rect.y - 5, size, size, raylib.WHITE);
    raylib.DrawRectangle(right - 5, rect.y - 5, size, size, raylib.WHITE);
    raylib.DrawRectangle(rect.x - 5, bottom - 5, size, size, raylib.WHITE);
    raylib.DrawRectangle(right - 5, bottom - 5, size, size, raylib.WHITE);
}

fn drawToolbar(position: toolbar_layout.Point, size: toolbar_layout.Size, aspect: []const u8) void {
    const x = position.x;
    const y = position.y;
    raylib.DrawRectangle(x, y, @intCast(size.width), @intCast(size.height), raylib.Fade(raylib.BLACK, 0.82));
    raylib.DrawRectangleLines(x, y, @intCast(size.width), @intCast(size.height), raylib.Fade(raylib.WHITE, 0.22));

    drawPill(x + 12, y + 11, 92, 30, aspectLabel(aspect), raylib.Color{ .r = 45, .g = 48, .b = 54, .a = 255 });
    drawPill(x + 112, y + 11, 52, 30, "Area", raylib.Color{ .r = 62, .g = 96, .b = 148, .a = 255 });
    drawPill(x + 174, y + 11, 74, 30, "Capture", raylib.Color{ .r = 38, .g = 132, .b = 88, .a = 255 });
    drawPill(x + 258, y + 11, 42, 30, "Esc", raylib.Color{ .r = 82, .g = 48, .b = 48, .a = 255 });
}

fn drawPill(x: i32, y: i32, width: i32, height: i32, label: []const u8, color: @TypeOf(raylib.WHITE)) void {
    raylib.DrawRectangle(x, y, width, height, color);
    raylib.DrawText(label, x + 10, y + 8, 13, raylib.WHITE);
}
