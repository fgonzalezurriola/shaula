const std = @import("std");
const c = @cImport({
    @cInclude("cli/json.h");
});

test "capture Zig caller receives shared C JSON escaping" {
    const value = "/tmp/shaula/q\"uote\".png";
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);

    const status = c.shaula_json_string_escape(.{ .data = value.ptr, .length = value.len }, &output);
    try std.testing.expectEqual(c.SHAULA_JSON_STATUS_OK, status);
    try std.testing.expectEqualStrings("\"/tmp/shaula/q\\\"uote\\\".png\"", output.data[0..output.length]);
}
