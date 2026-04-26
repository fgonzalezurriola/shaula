const std = @import("std");
const raylib = @import("raylib");
const clay = @import("clay");

/// HUD State encapsulates the current aspect constraint and UI selections.
pub const HudState = struct {
    aspect: ?[]const u8 = null,

    pub fn init() HudState {
        return .{};
    }
};

/// Renders the HUD layout using Raylib primitives and Clay layouts.
///
/// Contract constraints:
/// - Rendering MUST NOT block selection event loops.
/// - The HUD only exposes the current supported surface: capture modes,
///   aspect constraint, and commit/cancel actions.
/// - Aspect text must reflect the active constraint without mutating layout
///   ownership outside the helper runtime.
pub fn render(state: *HudState, screen_width: f32, screen_height: f32, sel_x: f32, sel_y: f32, sel_w: f32, sel_h: f32) void {
    const aspect_label = state.aspect orelse "Free";

    // 1. Dim layer (entire screen slightly dimmed)
    raylib.DrawRectangle(0, 0, @as(i32, @intFromFloat(screen_width)), @as(i32, @intFromFloat(screen_height)), raylib.Fade(raylib.BLACK, 0.5));

    // 2. Selection frame
    raylib.DrawRectangleLines(@as(i32, @intFromFloat(sel_x)), @as(i32, @intFromFloat(sel_y)), @as(i32, @intFromFloat(sel_w)), @as(i32, @intFromFloat(sel_h)), raylib.WHITE);

    // 3. Live size badge
    raylib.DrawText("Clean capture", @as(i32, @intFromFloat(sel_x)), @as(i32, @intFromFloat(sel_y - 20)), 20, raylib.WHITE);

    // 4. Tool strip layout using Clay
    clay.beginLayout();
    {
        // Strip container
        clay.beginElement(.{
            .id = "tool_strip",
            .layout = .{ .direction = .horizontal, .padding = .{ .x = 10, .y = 10 }, .spacing = 10 },
            .rectangle = .{ .color = .{ 0, 0, 0, 200 }, .cornerRadius = 8 },
        });
        defer clay.endElement();
        
        // Aspect dropdown UI
        clay.beginElement(.{
            .id = "aspect_dropdown",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 50, 50, 50, 255 }, .cornerRadius = 4 },
            .text = .{ .content = aspectLabel(aspect_label), .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();

        clay.beginElement(.{
            .id = "mode_area",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 60, 90, 140, 255 }, .cornerRadius = 4 },
            .text = .{ .content = "Area", .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();

        clay.beginElement(.{
            .id = "mode_fullscreen",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 35, 35, 35, 255 }, .cornerRadius = 4 },
            .text = .{ .content = "Fullscreen", .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();

        clay.beginElement(.{
            .id = "mode_window",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 35, 35, 35, 255 }, .cornerRadius = 4 },
            .text = .{ .content = "Window", .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();

        clay.beginElement(.{
            .id = "btn_capture",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 39, 132, 90, 255 }, .cornerRadius = 4 },
            .text = .{ .content = "Capture", .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();

        clay.beginElement(.{
            .id = "btn_cancel",
            .layout = .{ .padding = .{ .x = 8, .y = 4 } },
            .rectangle = .{ .color = .{ 80, 42, 42, 255 }, .cornerRadius = 4 },
            .text = .{ .content = "Cancel", .fontSize = 16, .color = .{ 255, 255, 255, 255 } },
        });
        clay.endElement();
    }
    clay.endLayout();
}

fn aspectLabel(aspect: []const u8) []const u8 {
    if (std.mem.eql(u8, aspect, "Free")) return "Aspect: Free";
    return aspect;
}
