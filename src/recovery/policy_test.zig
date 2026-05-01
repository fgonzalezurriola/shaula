const std = @import("std");
const policy = @import("policy.zig");

test "overlay taxonomy mappings resolve deterministic exit codes" {
    try std.testing.expectEqual(@as(u8, 36), policy.exitCodeFor("ERR_OVERLAY_UNAVAILABLE"));
    try std.testing.expectEqual(@as(u8, 37), policy.exitCodeFor("ERR_OVERLAY_TIMEOUT"));
    try std.testing.expectEqual(@as(u8, 38), policy.exitCodeFor("ERR_OVERLAY_PROTOCOL_INVALID"));
}

test "overlay taxonomy mappings keep deterministic retry budgets" {
    try std.testing.expectEqual(@as(u8, 3), policy.retryBudgetFor("ERR_OVERLAY_UNAVAILABLE"));
    try std.testing.expectEqual(@as(u8, 3), policy.retryBudgetFor("ERR_OVERLAY_TIMEOUT"));
    try std.testing.expectEqual(@as(u8, 0), policy.retryBudgetFor("ERR_OVERLAY_PROTOCOL_INVALID"));
}
