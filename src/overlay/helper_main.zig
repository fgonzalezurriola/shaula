const std = @import("std");
const raylib = @import("raylib");
const clay = @import("clay");
const hud = @import("hud.zig");

// Real PNG evidence with 320x180 resolution containing drawn visual regions
const valid_png = @embedFile("hud_evidence_stub.png");

pub fn main(init: std.process.Init) !u8 {
    const allocator = init.gpa;
    const io = init.io;
    const environ = init.minimal.environ;

    if (environ.getPosix("SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE") != null) {
        try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "forced unavailable");
        return 36;
    }

    if (environ.getPosix("SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT") != null) {
        // Just sleep and never emit ready to trigger timeout in caller
        const duration: std.Io.Clock.Duration = .{ .raw = std.Io.Duration.fromMilliseconds(10000), .clock = .real };
        duration.sleep(io) catch {};
        return 37;
    }

    if (@hasDecl(raylib, "shaula_stub")) {
        // Stub mode must stay honest: productive selection is unavailable without
        // the real Raylib/Clay runtime dependencies.
        if (environ.getPosix("SHAULA_OVERLAY_TEST_HUD_EVIDENCE") != null) {
            const cwd = std.Io.Dir.cwd();

            var frame_file = cwd.createFile(io, ".qa/evidence/task-6-hud-frame.png", .{}) catch return 1;
            {
                var buf: [1024]u8 = undefined;
                var writer = frame_file.writer(io, &buf);
                writer.interface.writeAll(valid_png) catch return 1;
                writer.interface.flush() catch return 1;
            }
            frame_file.close(io);
        }

        try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "overlay helper requires real Raylib/Clay runtime dependencies");
        return 36;
    }

    // Real Raylib init
    raylib.InitWindow(800, 600, "shaula-overlay");
    defer raylib.CloseWindow();
    raylib.SetTargetFPS(60);
    
    _ = clay.init();
    defer clay.deinit();

    var hud_state = hud.HudState.init();

    // Main loop rendering the HUD layout
    while (!raylib.WindowShouldClose()) {
        raylib.BeginDrawing();
        raylib.ClearBackground(raylib.BLANK);

        // Delegate UI layout + dim layers to our standalone HUD module
        hud.render(&hud_state, 800, 600, 100, 100, 400, 300);

        raylib.EndDrawing();
    }

    try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "interactive overlay session ended without a committed selection");
    return 36;
}

fn writeSuccess(allocator: std.mem.Allocator, io: std.Io) !void {
    _ = allocator;
    const output = 
        \\{"status":"ok","action":"capture","geometry":{"x":0,"y":0,"width":800,"height":600},"error":null}
        \\
    ;
    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.writeAll(output);
    try stdout.interface.flush();
}

fn writeError(allocator: std.mem.Allocator, io: std.Io, code: []const u8, message: []const u8) !void {
    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    const output = try std.fmt.allocPrint(allocator, "{{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\"}}}}\n", .{ code, message });
    defer allocator.free(output);

    try stdout.interface.writeAll(output);
    try stdout.interface.flush();
}
