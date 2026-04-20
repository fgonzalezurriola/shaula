const std = @import("std");
const selection = @import("../selection/selection.zig");

pub fn runSelection(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    mode: selection.SelectionMode,
    constraint: selection.SelectionConstraint,
    is_dry_run: bool,
    simulate_cancel: bool,
) !selection.SelectionResult {
    _ = environ;

    if (simulate_cancel) {
        return selection.SelectionResult{
            .mode = mode,
            .aspect = constraint.aspect,
            .geometry = null,
            .cancelled = true,
        };
    }

    if (is_dry_run) {
        // Return deterministic base area coordinates for testing
        return selection.SelectionResult{
            .mode = mode,
            .aspect = constraint.aspect,
            .geometry = .{ .x = 100, .y = 100, .width = 400, .height = 300 },
            .cancelled = false,
        };
    }

    // In a real implementation this initializes Wayland/Niri protocols and renders an overlay.
    // For now, we shell out to slurp as a functional minimum (gray-screen dimming, area selection).
    const result = std.process.run(allocator, io, .{
        .argv = &.{ "slurp", "-b", "#80808080", "-c", "#FFFFFF" },
        .stdout_limit = .limited(1024),
        .stderr_limit = .limited(1024),
    }) catch {
        return selection.SelectionResult{
            .mode = mode,
            .aspect = constraint.aspect,
            .geometry = null,
            .cancelled = true,
        };
    };
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code != 0) {
                return selection.SelectionResult{
                    .mode = mode,
                    .aspect = constraint.aspect,
                    .geometry = null,
                    .cancelled = true,
                };
            }
        },
        else => {
            return selection.SelectionResult{
                .mode = mode,
                .aspect = constraint.aspect,
                .geometry = null,
                .cancelled = true,
            };
        },
    }

    // Parse stdout: "10,20 300x400"
    var x: i32 = 0;
    var y: i32 = 0;
    var w: u32 = 0;
    var h: u32 = 0;

    var it = std.mem.tokenizeAny(u8, result.stdout, " ,x\r\n");
    if (it.next()) |sx| x = std.fmt.parseInt(i32, sx, 10) catch 0;
    if (it.next()) |sy| y = std.fmt.parseInt(i32, sy, 10) catch 0;
    if (it.next()) |sw| w = std.fmt.parseInt(u32, sw, 10) catch 0;
    if (it.next()) |sh| h = std.fmt.parseInt(u32, sh, 10) catch 0;

    return selection.SelectionResult{
        .mode = mode,
        .aspect = constraint.aspect,
        .geometry = .{ .x = x, .y = y, .width = w, .height = h },
        .cancelled = false,
    };
}
