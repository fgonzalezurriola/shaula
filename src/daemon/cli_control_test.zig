const std = @import("std");
const cli_control = @import("cli_control.zig");

test "daemon command names are stable" {
    try std.testing.expectEqualStrings("daemon start", cli_control.daemonCommand("start"));
    try std.testing.expectEqualStrings("daemon", cli_control.daemonCommand("x"));
}
