const std = @import("std");
const cli_json = @import("../cli/json.zig");

pub fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool) !void {
    try cli_json.writeBasicError(io, command, code, message, retryable);
}

pub const nowIso8601 = cli_json.nowIso8601;
