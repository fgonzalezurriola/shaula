const std = @import("std");

pub const PreviewAction = enum {
    close,
    copy,
    save,
    discard,

    pub fn asString(action: PreviewAction) []const u8 {
        return switch (action) {
            .close => "close",
            .copy => "copy",
            .save => "save",
            .discard => "discard",
        };
    }
};

pub const PreviewRunResult = struct {
    path: []const u8,
    closed: bool = true,
    action: PreviewAction = .close,
    copied: bool = false,
    saved: bool = false,
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

    const result = std.process.run(allocator, io, .{
        .argv = &.{ helper_bin, path },
        .stdout_limit = .limited(4096),
        .stderr_limit = .limited(4096),
    }) catch {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_UNAVAILABLE",
            .message = "preview helper is unavailable",
            .retryable = true,
        } };
    };
    defer allocator.free(result.stdout);
    defer allocator.free(result.stderr);

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

    return .{ .success = try parseHelperResult(allocator, path, result.stdout) };
}

fn parseHelperResult(allocator: std.mem.Allocator, path: []const u8, stdout: []const u8) !PreviewRunResult {
    const trimmed = std.mem.trim(u8, stdout, " \t\r\n");
    if (trimmed.len == 0) {
        return .{ .path = path };
    }

    var parsed = std.json.parseFromSlice(std.json.Value, allocator, trimmed, .{}) catch {
        return .{ .path = path };
    };
    defer parsed.deinit();

    const object = switch (parsed.value) {
        .object => |object| object,
        else => return .{ .path = path },
    };

    var result: PreviewRunResult = .{ .path = path };
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
    if (object.get("saved_path")) |value| {
        if (value == .string and value.string.len > 0) {
            result.saved_path = try allocator.dupe(u8, value.string);
        }
    }
    return result;
}

fn parseAction(value: []const u8) PreviewAction {
    if (std.mem.eql(u8, value, "copy")) return .copy;
    if (std.mem.eql(u8, value, "save")) return .save;
    if (std.mem.eql(u8, value, "discard")) return .discard;
    return .close;
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

test "preview helper result defaults to close on empty stdout" {
    const result = try parseHelperResult(std.testing.allocator, "/tmp/a.png", "");
    try std.testing.expectEqual(PreviewAction.close, result.action);
    try std.testing.expect(!result.copied);
    try std.testing.expect(!result.saved);
}

test "preview helper result parses save action" {
    var result = try parseHelperResult(std.testing.allocator, "/tmp/a.png", "{\"closed\":true,\"action\":\"save\",\"copied\":false,\"saved\":true,\"saved_path\":\"/tmp/b.png\"}\n");
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.save, result.action);
    try std.testing.expect(result.saved);
    try std.testing.expectEqualStrings("/tmp/b.png", result.saved_path.?);
}
