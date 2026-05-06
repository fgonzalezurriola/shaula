const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const capture_types = @import("../capture/types.zig");
const history_store = @import("../history/store.zig");
const clipboard_service = @import("../clipboard/service.zig");
const preview_service = @import("../preview/service.zig");
const cli_json = @import("../cli/json.zig");

pub const PipelineFlags = struct {
    save: bool,
    copy: bool,
    preview: bool,
};

const PreviewPipelineResult = struct {
    attempted: bool = false,
    ok: bool = false,
    code: ?[]const u8 = null,
    message: ?[]const u8 = null,
    action: ?preview_service.PreviewAction = null,
    copied: bool = false,
    saved: bool = false,
    saved_path: ?[]u8 = null,

    fn deinit(self: *PreviewPipelineResult, allocator: std.mem.Allocator) void {
        if (self.saved_path) |value| allocator.free(value);
        self.saved_path = null;
    }
};

const DegradedActionResult = struct {
    ok: bool = true,
    code: ?[]const u8 = null,
    message: ?[]const u8 = null,
};

const PostCaptureOutcome = struct {
    timestamp: []u8,
    saved: DegradedActionResult = .{},
    clipboard: DegradedActionResult = .{},
    preview: PreviewPipelineResult = .{},

    fn deinit(self: *PostCaptureOutcome, allocator: std.mem.Allocator) void {
        allocator.free(self.timestamp);
        self.preview.deinit(allocator);
        self.* = undefined;
    }
};

/// Emit the post-capture JSON envelope after optional save/copy/preview work.
///
/// Contract constraints:
/// - never suppress backend success payload, instead report `saved`/`clipboard`
///   and `preview` fields with deterministic `ERR_*` codes.
/// - preview is explicitly degraded; helper failures never invalidate the
///   already-created capture artifact.
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

    return writeCapturePipelineJsonWithResolvedFields(allocator, io, command, reported_mode, success, flags, outcome, warnings);
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
) !PostCaptureOutcome {
    var outcome = PostCaptureOutcome{
        .timestamp = try cli_json.nowIso8601(allocator, io),
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

    if (flags.copy) {
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
        var preview_outcome = try preview_service.runPreview(allocator, io, environ, success.path);
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

    return outcome;
}

fn writeCapturePipelineJsonWithResolvedFields(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    flags: PipelineFlags,
    outcome: PostCaptureOutcome,
    warnings: []const []const u8,
) !void {
    const partial = (flags.save and outcome.saved.ok) and (flags.copy and !outcome.clipboard.ok);
    const overall_ok = partial or ((!flags.save or outcome.saved.ok) and (!flags.copy or outcome.clipboard.ok));

    const command_json = try cli_json.stringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try cli_json.stringAlloc(allocator, outcome.timestamp);
    defer allocator.free(ts_json);
    const mode_json = try cli_json.stringAlloc(allocator, reported_mode);
    defer allocator.free(mode_json);
    const path_json = try cli_json.stringAlloc(allocator, success.path);
    defer allocator.free(path_json);
    const mime_json = try cli_json.stringAlloc(allocator, success.mime);
    defer allocator.free(mime_json);
    const backend_json = try cli_json.stringAlloc(allocator, success.backend_used);
    defer allocator.free(backend_json);

    const saved_error_json = try errorJsonField(allocator, outcome.saved.code, outcome.saved.message);
    defer allocator.free(saved_error_json);

    const clipboard_error_json = try errorJsonField(allocator, outcome.clipboard.code, outcome.clipboard.message);
    defer allocator.free(clipboard_error_json);
    const preview_json = try previewJsonField(allocator, outcome.preview);
    defer allocator.free(preview_json);
    const warnings_json = try cli_json.warningsAlloc(allocator, warnings);
    defer allocator.free(warnings_json);

    const saved_report_ok = flags.save and outcome.saved.ok;
    const clipboard_report_ok = flags.copy and outcome.clipboard.ok;
    const saved_report_path_json = if (flags.save) try allocator.dupe(u8, path_json) else try allocator.dupe(u8, "null");
    defer allocator.free(saved_report_path_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":{s},\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"degraded\":{s},\"saved\":{{\"ok\":{s},\"path\":{s},\"error\":{s}}},\"clipboard\":{{\"ok\":{s},\"error\":{s}}},\"preview\":{s},\"partial\":{s},\"result\":{{\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"saved\":{{\"ok\":{s},\"path\":{s}}},\"clipboard\":{{\"ok\":{s}}},\"preview\":{s}}},\"warnings\":{s}}}\n",
        .{
            if (overall_ok) "true" else "false",
            protocol.contract_version,
            command_json,
            ts_json,
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            if (success.degraded) "true" else "false",
            if (saved_report_ok) "true" else "false",
            saved_report_path_json,
            saved_error_json,
            if (clipboard_report_ok) "true" else "false",
            clipboard_error_json,
            preview_json,
            if (partial) "true" else "false",
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            if (saved_report_ok) "true" else "false",
            saved_report_path_json,
            if (clipboard_report_ok) "true" else "false",
            preview_json,
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

fn errorJsonField(allocator: std.mem.Allocator, code: ?[]const u8, message: ?[]const u8) ![]u8 {
    if (code == null or message == null) {
        return allocator.dupe(u8, "null");
    }
    const code_json = try cli_json.stringAlloc(allocator, code.?);
    defer allocator.free(code_json);
    const message_json = try cli_json.stringAlloc(allocator, message.?);
    defer allocator.free(message_json);
    return std.fmt.allocPrint(allocator, "{{\"code\":{s},\"message\":{s}}}", .{ code_json, message_json });
}

fn previewJsonField(allocator: std.mem.Allocator, result: PreviewPipelineResult) ![]u8 {
    const error_json = try errorJsonField(allocator, result.code, result.message);
    defer allocator.free(error_json);

    const action_json = if (result.action) |action|
        try jsonStringAlloc(allocator, action.asString())
    else
        try allocator.dupe(u8, "null");
    defer allocator.free(action_json);

    const saved_path_json = try cli_json.nullableStringAlloc(allocator, result.saved_path);
    defer allocator.free(saved_path_json);

    return std.fmt.allocPrint(
        allocator,
        "{{\"attempted\":{s},\"ok\":{s},\"error\":{s},\"action\":{s},\"copied\":{s},\"saved\":{s},\"saved_path\":{s}}}",
        .{
            if (result.attempted) "true" else "false",
            if (result.ok) "true" else "false",
            error_json,
            action_json,
            if (result.copied) "true" else "false",
            if (result.saved) "true" else "false",
            saved_path_json,
        },
    );
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return cli_json.stringAlloc(allocator, value);
}

test "pipeline error json field escapes quotes" {
    const encoded = try errorJsonField(std.testing.allocator, "ERR_TEST", "quoted \"message\"");
    defer std.testing.allocator.free(encoded);
    try std.testing.expect(std.mem.indexOf(u8, encoded, "\\\"message\\\"") != null);
}

test "pipeline preview json reports unattempted state" {
    const encoded = try previewJsonField(std.testing.allocator, .{});
    defer std.testing.allocator.free(encoded);
    try std.testing.expect(std.mem.indexOf(u8, encoded, "\"attempted\":false") != null);
    try std.testing.expect(std.mem.indexOf(u8, encoded, "\"error\":null") != null);
}

test "pipeline preview json reports degraded error" {
    const encoded = try previewJsonField(std.testing.allocator, .{
        .attempted = true,
        .ok = false,
        .code = "ERR_PREVIEW_UNAVAILABLE",
        .message = "preview helper is unavailable",
    });
    defer std.testing.allocator.free(encoded);
    try std.testing.expect(std.mem.indexOf(u8, encoded, "ERR_PREVIEW_UNAVAILABLE") != null);
}
