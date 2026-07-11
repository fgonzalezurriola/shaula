const std = @import("std");

const capture_types = @import("types.zig");
const history_store = @import("../history/store.zig");
const clipboard_service = @import("../clipboard/service.zig");
const preview_service = @import("../preview/service.zig");
const notify = @import("../notify.zig");
const post_capture_json = @import("post_capture_json.zig");
const post_capture_types = @import("post_capture_types.zig");
const c = @cImport({
    @cInclude("cli/json.h");
});

pub const PipelineFlags = struct {
    save: bool,
    copy: bool,
    preview: bool,
    show_success_notifications: bool = true,
    show_error_notifications: bool = true,
    include_notification_thumbnail: bool = true,
};

/// Emit the post-capture JSON envelope after optional save/copy/preview work.
///
/// Contract constraints:
/// - never suppress backend success payload, instead report `saved`/`clipboard`
///   and `preview` fields with deterministic `ERR_*` codes.
/// - preview is explicitly degraded; helper failures never invalidate the
///   already-created capture artifact.
/// - copy means immediate clipboard work when preview is skipped, or copy on
///   preview accept when preview is shown.
/// - keep JSON contract version and fields stable for QA assertions.
pub fn writeCapturePipelineJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    flags: PipelineFlags,
    warnings: []const []const u8,
) !void {
    var outcome = try runPostCaptureWork(allocator, io, environ, success, flags);
    defer outcome.deinit(allocator);

    return post_capture_json.write(allocator, io, command, reported_mode, success, flags, outcome, warnings);
}

/// Runs degraded post-capture work after the artifact is already available.
///
/// Contract constraint: history, clipboard, and preview failures are captured as
/// typed degraded outcomes so rendering cannot change hot-path side effects.
fn runPostCaptureWork(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    success: capture_types.CaptureSuccess,
    flags: PipelineFlags,
) !post_capture_types.PostCaptureOutcome {
    var outcome = post_capture_types.PostCaptureOutcome{
        .timestamp = try jsonTimestampAlloc(allocator, io),
    };
    errdefer outcome.deinit(allocator);

    if (flags.save) {
        history_store.storeLatest(io, .{
            .path = success.path,
            .mime = success.mime,
            .width = success.dimensions.width,
            .height = success.dimensions.height,
            .backend_used = success.backend_used,
            .timestamp = outcome.timestamp,
        }) catch {
            outcome.saved = .{
                .ok = false,
                .code = "ERR_HISTORY_STORE_UNAVAILABLE",
                .message = "history store unavailable",
            };
        };
    }

    if (flags.copy and !flags.preview) {
        if (clipboard_service.copyImage(io, environ, success.path)) |copy_result| {
            outcome.clipboard = .{
                .ok = copy_result.ok,
                .code = copy_result.code,
                .message = copy_result.message,
            };
        } else |_| {
            outcome.clipboard = .{
                .ok = false,
                .code = "ERR_UNKNOWN_UNMAPPED",
                .message = "clipboard copy failed with unmapped error",
            };
        }
    }

    if (flags.preview) {
        outcome.preview.attempted = true;
        var preview_outcome = try preview_service.runPreview(allocator, io, environ, success.path, flags.copy);
        switch (preview_outcome) {
            .success => |*preview_success| {
                defer preview_success.deinit(allocator);
                outcome.preview.ok = true;
                outcome.preview.action = preview_success.action;
                outcome.preview.copied = preview_success.copied;
                outcome.preview.saved = preview_success.saved;
                if (preview_success.saved_path) |saved_path| {
                    outcome.preview.saved_path = try allocator.dupe(u8, saved_path);
                }
            },
            .failure => |failure| {
                outcome.preview.ok = false;
                outcome.preview.code = failure.code;
                outcome.preview.message = failure.message;
            },
        }
    }

    if (!flags.preview) {
        notifyForDirectPipeline(allocator, io, flags, outcome, success.path) catch {};
    }

    return outcome;
}

/// Emits best-effort desktop notifications for direct capture pipelines.
///
/// Contract constraint: notification failures never change capture JSON or
/// `ERR_*` outcomes; preview owns its own notification path to avoid duplicates.
fn jsonTimestampAlloc(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_timestamp_from_unix_seconds(std.Io.Timestamp.now(io, .real).toSeconds(), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn notifyForDirectPipeline(
    allocator: std.mem.Allocator,
    io: std.Io,
    flags: PipelineFlags,
    outcome: post_capture_types.PostCaptureOutcome,
    path: []const u8,
) !void {
    if (flags.copy) {
        if (outcome.clipboard.ok) {
            if (flags.show_success_notifications) {
                if (flags.include_notification_thumbnail)
                    try notify.notifyScreenshotCopiedImage(allocator, io, path)
                else
                    try notify.notifyScreenshotCopied(allocator, io);
            }
        } else {
            if (flags.show_error_notifications)
                try notify.notifyScreenshotCopyFailed(allocator, io, outcome.clipboard.message orelse "Copy failed");
        }
        return;
    }

    if (flags.save) {
        if (outcome.saved.ok) {
            if (flags.show_success_notifications) {
                if (flags.include_notification_thumbnail)
                    try notify.notifyScreenshotSaved(allocator, io, path)
                else
                    try notify.notifyScreenshotSavedText(allocator, io, path);
            }
        } else {
            if (flags.show_error_notifications)
                try notify.notifyScreenshotSaveFailed(allocator, io, outcome.saved.message orelse "Save failed");
        }
    }
}
