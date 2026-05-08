const std = @import("std");

const notify = @import("../notify.zig");
const preview_result = @import("../preview_result.zig");
const process_exec = @import("../runtime/process_exec.zig");

pub const PreviewAction = preview_result.PreviewAction;

pub const PreviewRunResult = struct {
    path: []const u8,
    closed: bool = true,
    action: PreviewAction = .close,
    copied: bool = false,
    saved: bool = false,
    notified: bool = false,
    saved_path: ?[]u8 = null,

    pub fn deinit(self: *PreviewRunResult, allocator: std.mem.Allocator) void {
        if (self.saved_path) |value| allocator.free(value);
        self.saved_path = null;
    }
};

pub const PreviewFailure = struct {
    code: []const u8,
    message: []const u8,
    retryable: bool,
};

pub const PreviewOutcome = union(enum) {
    success: PreviewRunResult,
    failure: PreviewFailure,
};

/// Launches the native preview helper and maps helper/runtime failures to stable
/// preview `ERR_*` outcomes used by both CLI and post-capture orchestration.
///
/// The GTK helper owns immediate save/copy notifications. This boundary keeps a
/// fallback path only when the helper could not emit one, so callers never get
/// duplicate banners.
pub fn runPreview(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    path: []const u8,
) !PreviewOutcome {
    std.Io.Dir.cwd().access(io, path, .{}) catch {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_INPUT_INVALID",
            .message = "preview input image is not readable",
            .retryable = false,
        } };
    };

    const helper_bin = try resolvePreviewBinary(allocator, io, environ);
    defer allocator.free(helper_bin);

    const result = process_exec.run(allocator, io, &.{ helper_bin, path }, 4096, 4096) catch {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_UNAVAILABLE",
            .message = "preview helper is unavailable",
            .retryable = true,
        } };
    };
    defer result.deinit(allocator);

    switch (result.term) {
        .exited => |code| {
            if (code == 43) {
                return .{ .failure = .{
                    .code = "ERR_PREVIEW_INPUT_INVALID",
                    .message = "preview input image is invalid",
                    .retryable = false,
                } };
            }
            if (code != 0) {
                return .{ .failure = .{
                    .code = "ERR_PREVIEW_UNAVAILABLE",
                    .message = "preview helper exited unsuccessfully",
                    .retryable = true,
                } };
            }
        },
        else => {
            return .{ .failure = .{
                .code = "ERR_PREVIEW_UNAVAILABLE",
                .message = "preview helper terminated unexpectedly",
                .retryable = true,
            } };
        },
    }

    const helper_result = parseHelperResult(allocator, path, result.stdout) catch {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_RESULT_INVALID",
            .message = "preview helper did not emit valid result JSON",
            .retryable = true,
        } };
    };
    errdefer {
        var cleanup = helper_result;
        cleanup.deinit(allocator);
    }

    if (!helper_result.notified) {
        notifyForPreviewResult(allocator, io, helper_result) catch {};
    }

    return .{ .success = helper_result };
}

fn parseHelperResult(allocator: std.mem.Allocator, path: []const u8, stdout: []const u8) !PreviewRunResult {
    var parsed = try preview_result.parse(allocator, stdout);
    errdefer parsed.deinit(allocator);

    return .{
        .path = path,
        .closed = parsed.closed,
        .action = parsed.action,
        .copied = parsed.copied,
        .saved = parsed.saved,
        .notified = parsed.notified,
        .saved_path = parsed.saved_path,
    };
}

fn notifyForPreviewResult(allocator: std.mem.Allocator, io: std.Io, result: PreviewRunResult) !void {
    switch (result.action) {
        .save => {
            if (result.saved) {
                try notify.notifyScreenshotSaved(allocator, io, result.saved_path orelse "");
            } else {
                try notify.notifyScreenshotSaveFailed(allocator, io, "Save failed");
            }
        },
        .copy => {
            if (result.copied) {
                try notify.notifyScreenshotCopied(allocator, io);
            } else {
                try notify.notifyScreenshotCopyFailed(allocator, io, "Copy failed");
            }
        },
        .discard, .close, .unknown => {},
    }
}

fn resolvePreviewBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_PREVIEW_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-preview");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-preview", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-preview");
    }
}

test "preview helper result rejects empty stdout" {
    try std.testing.expectError(error.PreviewResultMissing, parseHelperResult(std.testing.allocator, "/tmp/a.png", ""));
}

test "preview helper result parses save action" {
    var result = try parseHelperResult(std.testing.allocator, "/tmp/a.png", "{\"closed\":true,\"action\":\"save\",\"copied\":false,\"saved\":true,\"saved_path\":\"/tmp/b.png\"}\n");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.save, result.action);
    try std.testing.expect(result.saved);
    try std.testing.expectEqualStrings("/tmp/b.png", result.saved_path.?);
}
