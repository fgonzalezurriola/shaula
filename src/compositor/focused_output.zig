const std = @import("std");
const compositor_runtime = @import("runtime.zig");
const c = @cImport({
    @cInclude("runtime/env.h");
    @cInclude("runtime/process_exec.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn envTrimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
    if (c.shaula_env_value_trimmed(envValue(environ, key), &result) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return result.data[0..result.length];
}

const ProcessResult = struct {
    output: c.ShaulaProcessOutput,

    fn deinit(self: *ProcessResult) void {
        c.shaula_process_output_clear(&self.output);
    }

    fn exitedZero(self: ProcessResult) bool {
        return self.output.term_kind == c.SHAULA_PROCESS_TERM_EXITED and self.output.term_value == 0;
    }

    fn stdout(self: ProcessResult) []const u8 {
        return self.output.stdout_bytes.data[0..self.output.stdout_bytes.length];
    }
};

fn runProcess(
    allocator: std.mem.Allocator,
    argv: []const []const u8,
    stdout_limit: usize,
    stderr_limit: usize,
) !ProcessResult {
    const spans = try allocator.alloc(c.ShaulaProcessSpan, argv.len);
    defer allocator.free(spans);
    for (argv, spans) |value, *span| {
        span.* = .{ .data = value.ptr, .length = value.len };
    }

    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    errdefer c.shaula_process_output_clear(&output);
    const status = c.shaula_process_run(
        .{ .items = spans.ptr, .length = spans.len },
        null,
        stdout_limit,
        stderr_limit,
        &output,
    );
    if (status != c.SHAULA_PROCESS_STATUS_OK) return error.ProcessFailed;
    return .{ .output = output };
}

const NiriFocusedOutput = struct {
    name: []const u8,
};

const SwayOutput = struct {
    name: []const u8,
    focused: bool = false,
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
    if (envTrimmed(environ, "SHAULA_OVERLAY_OUTPUT_NAME")) |raw| {
        return try allocator.dupe(u8, raw);
    }

    const compositor = compositor_runtime.detect(environ);
    if (compositor.kind == .niri) return resolveNiriFocusedOutputName(allocator, io);
    if (std.ascii.eqlIgnoreCase(compositor.label, "sway")) return resolveSwayFocusedOutputName(allocator, io);
    return null;
}

fn resolveNiriFocusedOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
) !?[]u8 {
    _ = io;
    var result = runProcess(allocator, &.{ "niri", "msg", "-j", "focused-output" }, 8192, 1024) catch return null;
    defer result.deinit();
    if (!result.exitedZero()) return null;

    const parsed = std.json.parseFromSlice(NiriFocusedOutput, allocator, result.stdout(), .{
        .ignore_unknown_fields = true,
    }) catch return null;
    defer parsed.deinit();
    if (parsed.value.name.len == 0) return null;

    return try allocator.dupe(u8, parsed.value.name);
}

fn resolveSwayFocusedOutputName(
    allocator: std.mem.Allocator,
    io: std.Io,
) !?[]u8 {
    _ = io;
    var result = runProcess(allocator, &.{ "swaymsg", "-t", "get_outputs", "-r" }, 65536, 1024) catch return null;
    defer result.deinit();
    if (!result.exitedZero()) return null;

    const parsed = std.json.parseFromSlice([]SwayOutput, allocator, result.stdout(), .{
        .ignore_unknown_fields = true,
    }) catch return null;
    defer parsed.deinit();
    for (parsed.value) |output| {
        if (output.focused and output.name.len > 0) return try allocator.dupe(u8, output.name);
    }
    return null;
}
