const std = @import("std");

const config_loader = @import("../config/loader.zig");
const notify = @import("../notify.zig");
const c = @cImport({
    @cInclude("preview/preview_result.h");
    @cInclude("runtime/helper_resolution.h");
    @cInclude("runtime/process_exec.h");
});

fn helperSpan(value: []const u8) c.ShaulaRuntimeHelperSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn processSpan(value: []const u8) c.ShaulaProcessSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn resolveSiblingHelper(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    env_var: []const u8,
    binary_name: []const u8,
) ![]u8 {
    const executable_dir = std.process.executableDirPathAlloc(io, allocator) catch null;
    defer if (executable_dir) |path| allocator.free(path);

    var owned: c.ShaulaRuntimeHelperOwnedPath = .{ .data = null, .length = 0 };
    defer c.shaula_runtime_helper_owned_path_clear(&owned);
    const status = c.shaula_runtime_helper_resolve(
        envValue(environ, env_var),
        if (executable_dir) |path| helperSpan(path) else .{ .data = null, .length = 0 },
        helperSpan(binary_name),
        &owned,
    );
    return switch (status) {
        c.SHAULA_RUNTIME_HELPER_STATUS_OK => allocator.dupe(u8, owned.data[0..owned.length]),
        c.SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT => error.InvalidPath,
        c.SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY => error.OutOfMemory,
        else => error.HelperResolutionFailed,
    };
}

pub const PreviewAction = enum(i32) {
    close = c.SHAULA_PREVIEW_ACTION_CLOSE,
    copy = c.SHAULA_PREVIEW_ACTION_COPY,
    save = c.SHAULA_PREVIEW_ACTION_SAVE,
    discard = c.SHAULA_PREVIEW_ACTION_DISCARD,
    unknown = c.SHAULA_PREVIEW_ACTION_UNKNOWN,

    pub fn asString(action: PreviewAction) []const u8 {
        const token = c.shaula_preview_action_token(@intFromEnum(action));
        return token.data[0..token.length];
    }
};

fn previewActionFromC(action: c.ShaulaPreviewAction) PreviewAction {
    return switch (action) {
        c.SHAULA_PREVIEW_ACTION_CLOSE => .close,
        c.SHAULA_PREVIEW_ACTION_COPY => .copy,
        c.SHAULA_PREVIEW_ACTION_SAVE => .save,
        c.SHAULA_PREVIEW_ACTION_DISCARD => .discard,
        c.SHAULA_PREVIEW_ACTION_UNKNOWN => .unknown,
        else => .unknown,
    };
}

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
    copy_on_accept: bool,
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
    const shaula_bin = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(shaula_bin);

    var helper_env = try std.process.Environ.createMap(environ, allocator);
    defer helper_env.deinit();
    try helper_env.put("SHAULA_BIN", shaula_bin);
    try helper_env.put("SHAULA_PREVIEW_COPY_ON_ACCEPT", if (copy_on_accept) "1" else "0");
    var loaded_config = config_loader.load(allocator, io, environ) catch null;
    defer if (loaded_config) |*loaded| loaded.deinit(allocator);
    try helper_env.put(
        "SHAULA_PREVIEW_CLOSE_ON_SAVE",
        if (loaded_config) |loaded| if (loaded.config.preview.window.close_preview_on_save) "1" else "0" else "0",
    );
    if (loaded_config) |loaded| {
        if (loaded.config.preview.window.width) |width| {
            const value = try std.fmt.allocPrint(allocator, "{d}", .{width});
            defer allocator.free(value);
            try helper_env.put("SHAULA_PREVIEW_WINDOW_WIDTH", value);
        }
        if (loaded.config.preview.window.height) |height| {
            const value = try std.fmt.allocPrint(allocator, "{d}", .{height});
            defer allocator.free(value);
            try helper_env.put("SHAULA_PREVIEW_WINDOW_HEIGHT", value);
        }
        const save_folder = loaded.config.capture.after.save_folder.value();
        if (save_folder.len > 0) try helper_env.put("SHAULA_SAVE_FOLDER", save_folder);
        try helper_env.put("SHAULA_NOTIFY_SUCCESS", if (loaded.config.notifications.success) "1" else "0");
        try helper_env.put("SHAULA_NOTIFY_ERRORS", if (loaded.config.notifications.errors) "1" else "0");
        try helper_env.put("SHAULA_NOTIFY_THUMBNAILS", if (loaded.config.notifications.thumbnails) "1" else "0");
    }

    const argv = [_]c.ShaulaProcessSpan{ processSpan(helper_bin), processSpan(path) };
    const environment = helper_env.createPosixBlock(allocator, .{}) catch {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_UNAVAILABLE",
            .message = "preview helper is unavailable",
            .retryable = true,
        } };
    };
    defer environment.deinit(allocator);

    var output: c.ShaulaProcessOutput = std.mem.zeroes(c.ShaulaProcessOutput);
    defer c.shaula_process_output_clear(&output);
    const process_status = c.shaula_process_run(
        .{ .items = &argv, .length = argv.len },
        @ptrCast(environment.slice.ptr),
        4096,
        4096,
        &output,
    );
    if (process_status != c.SHAULA_PROCESS_STATUS_OK) {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_UNAVAILABLE",
            .message = "preview helper is unavailable",
            .retryable = true,
        } };
    }

    if (output.term_kind == c.SHAULA_PROCESS_TERM_EXITED) {
        if (output.term_value == 43) {
            return .{ .failure = .{
                .code = "ERR_PREVIEW_INPUT_INVALID",
                .message = "preview input image is invalid",
                .retryable = false,
            } };
        }
        if (output.term_value != 0) {
            return .{ .failure = .{
                .code = "ERR_PREVIEW_UNAVAILABLE",
                .message = "preview helper exited unsuccessfully",
                .retryable = true,
            } };
        }
    } else {
        return .{ .failure = .{
            .code = "ERR_PREVIEW_UNAVAILABLE",
            .message = "preview helper terminated unexpectedly",
            .retryable = true,
        } };
    }

    const stdout = output.stdout_bytes.data[0..output.stdout_bytes.length];
    const helper_result = parseHelperResult(allocator, path, stdout) catch {
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
    var parsed: c.ShaulaPreviewResult = undefined;
    c.shaula_preview_result_init(&parsed);
    defer c.shaula_preview_result_clear(&parsed);

    const status = c.shaula_preview_result_parse(
        .{ .data = stdout.ptr, .length = stdout.len },
        &parsed,
    );
    switch (status) {
        c.SHAULA_PREVIEW_RESULT_STATUS_OK => {},
        c.SHAULA_PREVIEW_RESULT_STATUS_MISSING => return error.PreviewResultMissing,
        c.SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY => return error.OutOfMemory,
        c.SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON,
        c.SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT,
        => return error.PreviewResultInvalidJson,
        else => return error.PreviewResultInvalidJson,
    }

    const saved_path = if (parsed.saved_path.data != null)
        try allocator.dupe(u8, parsed.saved_path.data[0..parsed.saved_path.length])
    else
        null;
    errdefer if (saved_path) |value| allocator.free(value);

    return .{
        .path = path,
        .closed = parsed.closed != 0,
        .action = previewActionFromC(parsed.action),
        .copied = parsed.copied != 0,
        .saved = parsed.saved != 0,
        .notified = parsed.notified != 0,
        .saved_path = saved_path,
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
    return resolveSiblingHelper(allocator, io, environ, "SHAULA_PREVIEW_HELPER_BIN", "shaula-preview");
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

test "preview action tokens remain CLI compatible" {
    try std.testing.expectEqualStrings("close", PreviewAction.close.asString());
    try std.testing.expectEqualStrings("copy", PreviewAction.copy.asString());
    try std.testing.expectEqualStrings("save", PreviewAction.save.asString());
    try std.testing.expectEqualStrings("discard", PreviewAction.discard.asString());
    try std.testing.expectEqualStrings("unknown", PreviewAction.unknown.asString());
}

test "preview helper result preserves length-bearing saved path" {
    var result = try parseHelperResult(
        std.testing.allocator,
        "/tmp/a.png",
        "{\"action\":\"save\",\"saved_path\":\"a\\u0000b\"}",
    );
    defer result.deinit(std.testing.allocator);
    try std.testing.expectEqual(PreviewAction.save, result.action);
    try std.testing.expectEqualSlices(u8, "a\x00b", result.saved_path.?);
}
