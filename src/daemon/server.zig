const std = @import("std");
const protocol = @import("../ipc/protocol.zig");
const StateMachine = @import("state_machine.zig").StateMachine;
const State = @import("state_machine.zig").State;

pub const ServerError = error{
    IpcBindFailed,
    InvalidSocketPath,
    SocketParentCreateFailed,
};

pub const DaemonServer = struct {
    allocator: std.mem.Allocator,
    io: std.Io,
    socket_path: []const u8,
    machine: StateMachine,
    stop_requested: bool,

    pub fn init(allocator: std.mem.Allocator, io: std.Io, socket_path: []const u8) DaemonServer {
        return .{
            .allocator = allocator,
            .io = io,
            .socket_path = socket_path,
            .machine = StateMachine.init(),
            .stop_requested = false,
        };
    }

    pub fn run(self: *DaemonServer) ServerError!void {
        protocol.ensureSocketParent(self.socket_path, self.io) catch {
            self.machine.transition(.degraded) catch {};
            return error.SocketParentCreateFailed;
        };

        const unix_address = std.Io.net.UnixAddress.init(self.socket_path) catch {
            self.machine.transition(.@"error") catch {};
            return error.InvalidSocketPath;
        };

        var server = std.Io.net.UnixAddress.listen(&unix_address, self.io, .{}) catch {
            self.machine.transition(.@"error") catch {};
            return error.IpcBindFailed;
        };
        defer server.deinit(self.io);
        defer std.Io.Dir.deleteFileAbsolute(self.io, self.socket_path) catch {};

        self.machine.transition(.ready) catch {};

        while (!self.stop_requested) {
            var stream = server.accept(self.io) catch {
                self.machine.transition(.degraded) catch {};
                continue;
            };
            defer stream.close(self.io);

            self.handleConnection(stream) catch {
                self.machine.transition(.degraded) catch {};
            };
        }
    }

    fn handleConnection(self: *DaemonServer, stream: std.Io.net.Stream) !void {
        var read_buffer: [2048]u8 = undefined;
        var write_buffer: [2048]u8 = undefined;

        var reader = stream.reader(self.io, &read_buffer);
        var writer = stream.writer(self.io, &write_buffer);

        const request = reader.interface.takeDelimiterExclusive('\n') catch return;

        const parsed = std.json.parseFromSlice(std.json.Value, self.allocator, request, .{}) catch {
            try writeInvalidRequest(&writer.interface, "unknown", self.machine.current(), "invalid request payload");
            return;
        };
        defer parsed.deinit();

        const obj = switch (parsed.value) {
            .object => |root_object| root_object,
            else => {
                try writeInvalidRequest(&writer.interface, "unknown", self.machine.current(), "invalid request payload");
                return;
            },
        };

        const request_id_value = obj.get("request_id");
        const request_id: []const u8 = if (request_id_value) |rid| valueAsString(rid) orelse "unknown" else "unknown";

        const command_value = obj.get("command") orelse {
            try writeInvalidRequest(&writer.interface, request_id, self.machine.current(), "invalid request payload");
            return;
        };

        const command = valueAsString(command_value) orelse {
            try writeInvalidRequest(&writer.interface, request_id, self.machine.current(), "invalid request payload");
            return;
        };

        if (std.mem.eql(u8, command, "daemon.status")) {
            try self.writeStateResponse(&writer.interface, request_id, self.machine.current());
            return;
        }

        if (std.mem.eql(u8, command, "daemon.stop")) {
            self.stop_requested = true;
            try writer.interface.print(
                "{{\"ipc_version\":\"{s}\",\"request_id\":\"{s}\",\"ok\":true,\"result\":{{\"stopped\":true}},\"error\":null,\"degraded\":false,\"daemon_state\":\"{s}\",\"warnings\":[]}}\n",
                .{ protocol.ipc_version, request_id, self.machine.current().asString() },
            );
            try writer.interface.flush();
            return;
        }

        if (std.mem.eql(u8, command, "daemon.capture.begin")) {
            self.machine.transition(.capturing) catch {};
            try self.writeStateResponse(&writer.interface, request_id, self.machine.current());
            return;
        }

        if (std.mem.eql(u8, command, "daemon.capture.end")) {
            if (self.machine.current() == .capturing) {
                self.machine.transition(.ready) catch {};
            }
            try self.writeStateResponse(&writer.interface, request_id, self.machine.current());
            return;
        }

        const state = self.machine.current();
        try writer.interface.print(
            "{{\"ipc_version\":\"{s}\",\"request_id\":\"{s}\",\"ok\":false,\"result\":null,\"error\":{{\"code\":\"ERR_IPC_REQUEST_INVALID\",\"message\":\"unsupported command\",\"retryable\":false,\"details\":{{\"command\":\"{s}\"}}}},\"degraded\":true,\"daemon_state\":\"{s}\",\"warnings\":[]}}\n",
            .{ protocol.ipc_version, request_id, command, state.asString() },
        );
        try writer.interface.flush();
    }

    fn valueAsString(value: std.json.Value) ?[]const u8 {
        return switch (value) {
            .string => |s| s,
            else => null,
        };
    }

    fn writeInvalidRequest(io_writer: *std.Io.Writer, request_id: []const u8, state: State, message: []const u8) !void {
        try io_writer.print(
            "{{\"ipc_version\":\"{s}\",\"request_id\":\"{s}\",\"ok\":false,\"result\":null,\"error\":{{\"code\":\"ERR_IPC_REQUEST_INVALID\",\"message\":\"{s}\",\"retryable\":false,\"details\":{{}}}},\"degraded\":true,\"daemon_state\":\"{s}\",\"warnings\":[]}}\n",
            .{ protocol.ipc_version, request_id, message, state.asString() },
        );
        try io_writer.flush();
    }

    fn writeStateResponse(self: *DaemonServer, io_writer: *std.Io.Writer, request_id: []const u8, state: State) !void {
        _ = self;
        const degraded = state == .degraded;
        try io_writer.print(
            "{{\"ipc_version\":\"{s}\",\"request_id\":\"{s}\",\"ok\":true,\"result\":{{\"state\":\"{s}\",\"health\":\"ok\"}},\"error\":null,\"degraded\":{s},\"daemon_state\":\"{s}\",\"warnings\":[]}}\n",
            .{ protocol.ipc_version, request_id, state.asString(), if (degraded) "true" else "false", state.asString() },
        );
        try io_writer.flush();
    }
};

test "ipc valueAsString rejects non-string field" {
    try std.testing.expectEqualStrings("cmd", DaemonServer.valueAsString(.{ .string = "cmd" }).?);
    try std.testing.expect(DaemonServer.valueAsString(.{ .integer = 123 }) == null);
}
