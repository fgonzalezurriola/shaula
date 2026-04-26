const taxonomy = @import("../errors/taxonomy.zig");
const std = @import("std");

pub fn specFor(code: []const u8) taxonomy.ErrorSpec {
    return taxonomy.find(code) orelse taxonomy.unknownSpec();
}

/// Resolve deterministic process exit code for one taxonomy `ERR_*` code.
///
/// Contract constraint: unknown or unmapped inputs must always collapse to
/// `ERR_UNKNOWN_UNMAPPED` semantics via `specFor`, preserving stable exit code `99`.
pub fn exitCodeFor(code: []const u8) u8 {
    return specFor(code).exit_code;
}

pub fn retryBudgetFor(code: []const u8) u8 {
    const spec = specFor(code);
    if (!spec.retryable) return 0;
    return switch (spec.action) {
        .retry_limited => 3,
        .degrade_to_portal => 1,
        else => 0,
    };
}

test "overlay taxonomy mappings resolve deterministic exit codes" {
    try std.testing.expectEqual(@as(u8, 36), exitCodeFor("ERR_OVERLAY_UNAVAILABLE"));
    try std.testing.expectEqual(@as(u8, 37), exitCodeFor("ERR_OVERLAY_TIMEOUT"));
    try std.testing.expectEqual(@as(u8, 38), exitCodeFor("ERR_OVERLAY_PROTOCOL_INVALID"));
}

test "overlay taxonomy mappings keep deterministic retry budgets" {
    try std.testing.expectEqual(@as(u8, 3), retryBudgetFor("ERR_OVERLAY_UNAVAILABLE"));
    try std.testing.expectEqual(@as(u8, 3), retryBudgetFor("ERR_OVERLAY_TIMEOUT"));
    try std.testing.expectEqual(@as(u8, 0), retryBudgetFor("ERR_OVERLAY_PROTOCOL_INVALID"));
}
