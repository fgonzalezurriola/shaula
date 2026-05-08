const std = @import("std");
const compositor_runtime = @import("runtime.zig");
const process_exec = @import("../runtime/process_exec.zig");

const NiriFocusedOutput = struct {
    name: []const u8,
};

/// Resolve compositor-focused output name for monitor-scoped operations.
///
/// Contract constraints:
/// - returns null for unsupported/unknown compositors so callers keep
///   compositor-default behavior instead of fabricating an output.
/// - Niri resolution is best-effort and never escalates into `ERR_*`.
pub fn resolveName(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
) !?[]u8 {
    if (environ.getPosix("SHAULA_OVERLAY_OUTPUT_NAME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return try allocator.dupe(u8, raw);
    }

    const compositor = compositor_runtime.detect(environ);
    return switch (compositor.kind) {
        .niri => resolveNiriFocusedOutputName(allocator, io),
        .wayland, .unsupported => null,
    };
}

fn resolveNiriFocusedOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
) !?[]u8 {
    const result = process_exec.run(allocator, io, &.{ "niri", "msg", "-j", "focused-output" }, 8192, 1024) catch return null;
    defer result.deinit(allocator);
    if (!result.exitedZero()) return null;

    const parsed = std.json.parseFromSlice(NiriFocusedOutput, allocator, result.stdout, .{
        .ignore_unknown_fields = true,
    }) catch return null;
    defer parsed.deinit();
    if (parsed.value.name.len == 0) return null;

    return try allocator.dupe(u8, parsed.value.name);
}
