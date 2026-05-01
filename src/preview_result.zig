const std = @import("std");

pub const PreviewAction = enum {
    close,
    copy,
    save,
    discard,
    unknown,

    pub fn asString(action: PreviewAction) []const u8 {
        return switch (action) {
            .close => "close",
            .copy => "copy",
            .save => "save",
            .discard => "discard",
            .unknown => "unknown",
        };
    }
};

pub const PreviewResult = struct {
    closed: bool = false,
    action: PreviewAction = .unknown,
    copied: bool = false,
    saved: bool = false,
    /// Set by the helper when it already emitted the save/copy notification.
    notified: bool = false,
    saved_path: ?[]u8 = null,

    pub fn deinit(self: *PreviewResult, allocator: std.mem.Allocator) void {
        if (self.saved_path) |value| allocator.free(value);
        self.saved_path = null;
    }
};

/// Runtime boundary for the C GTK preview contract.
///
/// The helper must emit a final JSON object when it exits. Unknown action names
/// are accepted as `.unknown` so newly-built helpers do not crash older Zig
/// callers, while malformed or missing JSON maps to deterministic preview
/// `ERR_*` handling in the service layer. When present, `notified` means the
/// native helper already emitted the user-facing save/copy banner, so callers
/// can avoid duplicate fallback notifications.
pub fn parse(allocator: std.mem.Allocator, stdout: []const u8) !PreviewResult {
    const trimmed = std.mem.trim(u8, stdout, " \t\r\n");
    if (trimmed.len == 0) return error.PreviewResultMissing;

    var parsed = std.json.parseFromSlice(std.json.Value, allocator, trimmed, .{}) catch {
        return error.PreviewResultInvalidJson;
    };
    defer parsed.deinit();

    const object = switch (parsed.value) {
        .object => |object| object,
        else => return error.PreviewResultInvalidJson,
    };

    var result: PreviewResult = .{};
    errdefer result.deinit(allocator);

    if (object.get("closed")) |value| {
        if (value == .bool) result.closed = value.bool;
    }
    if (object.get("action")) |value| {
        if (value == .string) result.action = parseAction(value.string);
    }
    if (object.get("copied")) |value| {
        if (value == .bool) result.copied = value.bool;
    }
    if (object.get("saved")) |value| {
        if (value == .bool) result.saved = value.bool;
    }
    if (object.get("notified")) |value| {
        if (value == .bool) result.notified = value.bool;
    }
    if (object.get("saved_path")) |value| {
        if (value == .string and value.string.len > 0) {
            result.saved_path = try allocator.dupe(u8, value.string);
        }
    }

    return result;
}

fn parseAction(value: []const u8) PreviewAction {
    if (std.mem.eql(u8, value, "close")) return .close;
    if (std.mem.eql(u8, value, "copy")) return .copy;
    if (std.mem.eql(u8, value, "save")) return .save;
    if (std.mem.eql(u8, value, "discard")) return .discard;
    return .unknown;
}

test "preview result parses successful save" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"save\",\"copied\":false,\"saved\":true,\"saved_path\":\"/tmp/b.png\"}\n");
    defer result.deinit(std.testing.allocator);
    try std.testing.expect(result.closed);
    try std.testing.expectEqual(PreviewAction.save, result.action);
    try std.testing.expect(result.saved);
    try std.testing.expectEqualStrings("/tmp/b.png", result.saved_path.?);
}

test "preview result parses successful copy" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"copy\",\"copied\":true,\"saved\":false,\"saved_path\":null}");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.copy, result.action);
    try std.testing.expect(result.copied);
    try std.testing.expect(result.saved_path == null);
}

test "preview result parses discard" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"discard\",\"copied\":false,\"saved\":false}");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.discard, result.action);
}

test "preview result parses close" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"close\",\"copied\":false,\"saved\":false}");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.close, result.action);
}

test "preview result maps unknown action" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"pin\",\"copied\":false,\"saved\":false}");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.unknown, result.action);
}

test "preview result accepts missing saved path" {
    var result = try parse(std.testing.allocator, "{\"closed\":true,\"action\":\"save\",\"copied\":false,\"saved\":true}");
    defer result.deinit(std.testing.allocator);
    try std.testing.expect(result.saved_path == null);
}

test "preview result rejects invalid json" {
    try std.testing.expectError(error.PreviewResultInvalidJson, parse(std.testing.allocator, "{"));
}
