const taxonomy = @import("../errors/taxonomy.zig");

pub fn specFor(code: []const u8) taxonomy.ErrorSpec {
    return taxonomy.find(code) orelse taxonomy.unknownSpec();
}

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
