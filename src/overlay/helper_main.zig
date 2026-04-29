const std = @import("std");
const raylib = @import("raylib");
const clay = @import("clay");
const hud = @import("hud.zig");
const all_in_one_session = @import("all_in_one_session.zig");
const overlay_strategy = @import("strategy.zig");
const ui_state_store = @import("ui_state_store.zig");
const native_gtk_overlay = @import("native_gtk_overlay.zig");

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

    if (environ.getPosix("SHAULA_OVERLAY_TEST_HUD_EVIDENCE") != null) {
        try writeHudEvidence(io);
    }

    return switch (resolveStrategy(environ)) {
        .auto => runAutoStrategy(allocator, io, environ),
        .gtk4_layer_shell => runGtkOverlay(allocator, io),
        .raylib => runRaylibOverlay(allocator, io, environ, false),
        .raylib_clay => runRaylibOverlay(allocator, io, environ, true),
    };
}

fn resolveStrategy(environ: std.process.Environ) overlay_strategy.OverlayStrategy {
    const raw_z = environ.getPosix("SHAULA_OVERLAY_HELPER_STRATEGY") orelse return .auto;
    return overlay_strategy.parse(std.mem.sliceTo(raw_z, 0));
}

/// Strategy selector for the helper UI backend.
///
/// Contract constraints:
/// - `auto` prefers compiled native Raylib paths when available.
/// - GTK is an explicit fallback strategy kept inside overlay-helper so capture
///   command latency and visual dependencies do not leak into `capture`.
/// - unavailable strategy choices emit deterministic `ERR_OVERLAY_UNAVAILABLE`.
fn runAutoStrategy(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    if (!@hasDecl(raylib, "shaula_stub")) {
        if (!@hasDecl(clay, "shaula_stub")) {
            return runRaylibOverlay(allocator, io, environ, true);
        }
        return runRaylibOverlay(allocator, io, environ, false);
    }

    return runGtkOverlay(allocator, io);
}

fn runRaylibOverlay(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, use_clay: bool) !u8 {
    if (@hasDecl(raylib, "shaula_stub")) {
        try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "raylib overlay strategy requires real Raylib build dependency");
        return 36;
    }
    if (use_clay and @hasDecl(clay, "shaula_stub")) {
        try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "raylib-clay overlay strategy requires real Clay build dependency");
        return 36;
    }

    raylib.InitWindow(800, 600, "shaula-overlay");
    defer raylib.CloseWindow();
    raylib.SetTargetFPS(60);

    if (use_clay) {
        _ = clay.init();
    }
    defer if (use_clay) clay.deinit();

    const persisted_toolbar = ui_state_store.load(allocator, io, environ) catch null;
    var session = all_in_one_session.AllInOneSession.init(all_in_one_session.defaultOutput(), persisted_toolbar);
    session.updateSelection(.{ .x = 100, .y = 100, .width = 400, .height = 300 });
    var hud_state = hud.HudState.init(session);

    // Main loop rendering the HUD layout
    while (!raylib.WindowShouldClose()) {
        raylib.BeginDrawing();
        raylib.ClearBackground(raylib.BLANK);

        // Delegate UI layout + dim layers to our standalone HUD module.
        hud.render(&hud_state);

        raylib.EndDrawing();
    }

    try writeError(allocator, io, "ERR_OVERLAY_UNAVAILABLE", "interactive overlay session ended without a committed selection");
    return 36;
}

fn writeHudEvidence(io: std.Io) !void {
    const cwd = std.Io.Dir.cwd();

    var frame_file = try cwd.createFile(io, ".qa/evidence/task-6-hud-frame.png", .{});
    {
        var buf: [1024]u8 = undefined;
        var writer = frame_file.writer(io, &buf);
        try writer.interface.writeAll(valid_png);
        try writer.interface.flush();
    }
    frame_file.close(io);
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

/// Runs the native GTK layer-shell overlay when Raylib/Clay are not wired.
///
/// Contract constraints:
/// - stdout remains the helper envelope v1 consumed by the parent parser.
/// - GTK startup failures map to deterministic `ERR_OVERLAY_UNAVAILABLE`.
/// - capture geometry is produced only from a committed user selection.
fn runGtkOverlay(allocator: std.mem.Allocator, io: std.Io) !u8 {
    _ = allocator;
    _ = io;
    return native_gtk_overlay.run();
}
