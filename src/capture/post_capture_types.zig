const std = @import("std");

const preview_service = @import("../preview/service.zig");

pub const PreviewPipelineResult = struct {
    attempted: bool = false,
    ok: bool = false,
    code: ?[]const u8 = null,
    message: ?[]const u8 = null,
    action: ?preview_service.PreviewAction = null,
    copied: bool = false,
    saved: bool = false,
    saved_path: ?[]u8 = null,

    pub fn deinit(self: *PreviewPipelineResult, allocator: std.mem.Allocator) void {
        if (self.saved_path) |value| allocator.free(value);
        self.saved_path = null;
    }
};

pub const DegradedActionResult = struct {
    ok: bool = true,
    code: ?[]const u8 = null,
    message: ?[]const u8 = null,
};

pub const PostCaptureOutcome = struct {
    timestamp: []u8,
    saved: DegradedActionResult = .{},
    clipboard: DegradedActionResult = .{},
    preview: PreviewPipelineResult = .{},

    pub fn deinit(self: *PostCaptureOutcome, allocator: std.mem.Allocator) void {
        allocator.free(self.timestamp);
        self.preview.deinit(allocator);
        self.* = undefined;
    }
};
