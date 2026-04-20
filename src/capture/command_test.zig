const std = @import("std");
const json = @import("command_json.zig");

test "json string helper escapes embedded quotes" {
    const encoded = try json.jsonStringAlloc(std.testing.allocator, "/tmp/shaula/q\"uote\".png");
    defer std.testing.allocator.free(encoded);
    try std.testing.expectEqualStrings("\"/tmp/shaula/q\\\"uote\\\".png\"", encoded);
}
