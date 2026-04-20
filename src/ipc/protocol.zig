const std = @import("std");

pub const ipc_version = "1.0.0";
pub const contract_version = "1.0.0";

pub const LifecycleState = enum {
    initializing,
    ready,
    capturing,
    degraded,
    @"error",

    pub fn asString(state: LifecycleState) []const u8 {
        return switch (state) {
            .initializing => "initializing",
            .ready => "ready",
            .capturing => "capturing",
            .degraded => "degraded",
            .@"error" => "error",
        };
    }
};

pub fn defaultSocketPath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("XDG_RUNTIME_DIR")) |runtime_dir_z| {
        const runtime_dir = std.mem.sliceTo(runtime_dir_z, 0);
        return std.fmt.allocPrint(allocator, "{s}/shaula/daemon-v1.sock", .{runtime_dir});
    }

    const uid: u32 = uid: {
        if (environ.getPosix("UID")) |uid_z| {
            const uid_slice = std.mem.sliceTo(uid_z, 0);
            break :uid std.fmt.parseInt(u32, uid_slice, 10) catch 0;
        }
        break :uid 0;
    };
    return std.fmt.allocPrint(allocator, "/tmp/shaula-{d}/daemon-v1.sock", .{uid});
}

pub fn resolveSocketPath(allocator: std.mem.Allocator, environ: std.process.Environ, cli_socket: ?[]const u8) ![]u8 {
    if (cli_socket) |path| {
        return allocator.dupe(u8, path);
    }
    if (environ.getPosix("SHAULA_SOCKET")) |env_socket_z| {
        const env_socket = std.mem.sliceTo(env_socket_z, 0);
        return allocator.dupe(u8, env_socket);
    }
    return defaultSocketPath(allocator, environ);
}

pub fn ensureSocketParent(path: []const u8, io: std.Io) !void {
    const dir_path = std.fs.path.dirname(path) orelse return error.InvalidSocketPath;
    try std.Io.Dir.cwd().createDirPath(io, dir_path);
}
