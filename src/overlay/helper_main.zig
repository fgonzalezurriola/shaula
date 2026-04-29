const std = @import("std");
const native_gtk_overlay = @import("native_gtk_overlay.zig");

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

    return runGtkOverlay(allocator, io);
}

fn writeError(allocator: std.mem.Allocator, io: std.Io, code: []const u8, message: []const u8) !void {
    var stdout_buffer: [1024]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);

    const output = try std.fmt.allocPrint(allocator, "{{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{{\"code\":\"{s}\",\"message\":\"{s}\"}}}}\n", .{ code, message });
    defer allocator.free(output);

    try stdout.interface.writeAll(output);
    try stdout.interface.flush();
}

/// Runs the native GTK layer-shell overlay.
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
