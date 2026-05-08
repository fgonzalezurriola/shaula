const std = @import("std");

pub const ProcessOutput = struct {
    stdout: []u8,
    stderr: []u8,
    term: std.process.Child.Term,

    pub fn deinit(self: ProcessOutput, allocator: std.mem.Allocator) void {
        allocator.free(self.stdout);
        allocator.free(self.stderr);
    }

    pub fn exitedZero(self: ProcessOutput) bool {
        return switch (self.term) {
            .exited => |code| code == 0,
            else => false,
        };
    }
};

/// Runtime process adapter for command execution at capture/overlay seams.
///
/// Contract constraints: callers own `ProcessOutput.deinit`, stdout/stderr
/// limits must stay explicit at each call site, and spawn failures are surfaced
/// unchanged so caller modules preserve their deterministic `ERR_*` mapping.
pub fn run(
    allocator: std.mem.Allocator,
    io: std.Io,
    argv: []const []const u8,
    stdout_limit: usize,
    stderr_limit: usize,
) !ProcessOutput {
    const result = try std.process.run(allocator, io, .{
        .argv = argv,
        .stdout_limit = .limited(stdout_limit),
        .stderr_limit = .limited(stderr_limit),
    });
    return .{ .stdout = result.stdout, .stderr = result.stderr, .term = result.term };
}

/// Runtime process adapter variant for helper calls that need an environment.
pub fn runWithEnv(
    allocator: std.mem.Allocator,
    io: std.Io,
    argv: []const []const u8,
    stdout_limit: usize,
    stderr_limit: usize,
    environ_map: *std.process.Environ.Map,
) !ProcessOutput {
    const result = try std.process.run(allocator, io, .{
        .argv = argv,
        .stdout_limit = .limited(stdout_limit),
        .stderr_limit = .limited(stderr_limit),
        .environ_map = environ_map,
    });
    return .{ .stdout = result.stdout, .stderr = result.stderr, .term = result.term };
}

/// Runtime process adapter for commands that consume stdin bytes.
///
/// Contract constraints: this helper owns stdin write/close ordering; callers
/// keep deterministic `ERR_*` mapping based on the returned termination status.
pub fn runWithPipeInput(
    io: std.Io,
    argv: []const []const u8,
    input: []const u8,
) !std.process.Child.Term {
    var child = try std.process.spawn(io, .{
        .argv = argv,
        .stdin = .pipe,
        .stdout = .ignore,
        .stderr = .ignore,
    });

    errdefer if (child.id != null) child.kill(io);
    if (child.stdin) |stdin| {
        try stdin.writeStreamingAll(io, input);
        stdin.close(io);
        child.stdin = null;
    }

    return child.wait(io);
}
