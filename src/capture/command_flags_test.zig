const std = @import("std");
const command_flags = @import("command_flags.zig");

test "preview defaults follow interactive capture modes" {
    try std.testing.expect(command_flags.resolvePreviewDefault("area", null));
    try std.testing.expect(command_flags.resolvePreviewDefault("all-in-one", null));
    try std.testing.expect(!command_flags.resolvePreviewDefault("fullscreen", null));
    try std.testing.expect(!command_flags.resolvePreviewDefault("focused", null));
    try std.testing.expect(!command_flags.resolvePreviewDefault("window", null));
    try std.testing.expect(!command_flags.resolvePreviewDefault("previous-area", null));
    try std.testing.expect(command_flags.resolvePreviewDefault("fullscreen", true));
    try std.testing.expect(!command_flags.resolvePreviewDefault("area", false));
}
