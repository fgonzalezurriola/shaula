const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const capture_types = @import("../capture/types.zig");
const history_store = @import("../history/store.zig");
const clipboard_service = @import("../clipboard/service.zig");
const preview_service = @import("../preview/service.zig");
const capture_command_json = @import("../capture/command_json.zig");

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
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    var saved_ok = true;
    var saved_error_code: ?[]const u8 = null;
    var saved_error_message: ?[]const u8 = null;

    if (flags.save) {
        history_store.storeLatest(io, .{
            .path = success.path,
            .mime = success.mime,
            .width = success.dimensions.width,
            .height = success.dimensions.height,
            .backend_used = success.backend_used,
            .timestamp = ts,
        }) catch {
            saved_ok = false;
            saved_error_code = "ERR_HISTORY_STORE_UNAVAILABLE";
            saved_error_message = "history store unavailable";
        };
    }

    var clipboard_ok = true;
    var clipboard_error_code: ?[]const u8 = null;
    var clipboard_error_message: ?[]const u8 = null;

    if (flags.copy) {
        if (clipboard_service.copyImage(io, environ, success.path)) |copy_result| {
            clipboard_ok = copy_result.ok;
            clipboard_error_code = copy_result.code;
            clipboard_error_message = copy_result.message;
        } else |_| {
            clipboard_ok = false;
            clipboard_error_code = "ERR_UNKNOWN_UNMAPPED";
            clipboard_error_message = "clipboard copy failed with unmapped error";
        }
    }

    var preview_result: PreviewPipelineResult = .{};
    defer preview_result.deinit(allocator);
    if (flags.preview) {
        preview_result.attempted = true;
        var preview_outcome = try preview_service.runPreview(allocator, io, environ, success.path);
        switch (preview_outcome) {
            .success => |*preview_success| {
                defer preview_success.deinit(allocator);
                preview_result.ok = true;
                preview_result.action = preview_success.action;
                preview_result.copied = preview_success.copied;
                preview_result.saved = preview_success.saved;
                if (preview_success.saved_path) |saved_path| {
                    preview_result.saved_path = try allocator.dupe(u8, saved_path);
                }
            },
            .failure => |failure| {
                preview_result.ok = false;
                preview_result.code = failure.code;
                preview_result.message = failure.message;
            },
        }
    }

    return writeCapturePipelineJsonWithResolvedFields(allocator, io, command, reported_mode, success, flags, ts, saved_ok, saved_error_code, saved_error_message, clipboard_ok, clipboard_error_code, clipboard_error_message, preview_result, warnings);
}

fn writeCapturePipelineJsonWithResolvedFields(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    flags: PipelineFlags,
    ts: []const u8,
    saved_ok: bool,
    saved_error_code: ?[]const u8,
    saved_error_message: ?[]const u8,
    clipboard_ok: bool,
    clipboard_error_code: ?[]const u8,
    clipboard_error_message: ?[]const u8,
    preview_result: PreviewPipelineResult,
    warnings: []const []const u8,
) !void {

    const partial = (flags.save and saved_ok) and (flags.copy and !clipboard_ok);
    const overall_ok = partial or ((!flags.save or saved_ok) and (!flags.copy or clipboard_ok));

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const mode_json = try jsonStringAlloc(allocator, reported_mode);
    defer allocator.free(mode_json);
    const path_json = try jsonStringAlloc(allocator, success.path);
    defer allocator.free(path_json);
    const mime_json = try jsonStringAlloc(allocator, success.mime);
    defer allocator.free(mime_json);
    const backend_json = try jsonStringAlloc(allocator, success.backend_used);
    defer allocator.free(backend_json);

    const saved_error_json = try errorJsonField(allocator, saved_error_code, saved_error_message);
    defer allocator.free(saved_error_json);

    const clipboard_error_json = try errorJsonField(allocator, clipboard_error_code, clipboard_error_message);
    defer allocator.free(clipboard_error_json);
    const preview_json = try previewJsonField(allocator, preview_result);
    defer allocator.free(preview_json);
    const warnings_json = try capture_command_json.warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    const saved_report_ok = flags.save and saved_ok;
    const clipboard_report_ok = flags.copy and clipboard_ok;
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
    const code_json = try jsonStringAlloc(allocator, code.?);
    defer allocator.free(code_json);
    const message_json = try jsonStringAlloc(allocator, message.?);
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

    const saved_path_json = try jsonNullableStringAlloc(allocator, result.saved_path);
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
    return std.json.Stringify.valueAlloc(allocator, value, .{});
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    if (value) |text| return jsonStringAlloc(allocator, text);
    return allocator.dupe(u8, "null");
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    const ts = std.Io.Timestamp.now(io, .real);
    const epoch_seconds: i64 = ts.toSeconds();

    const days: i64 = @divFloor(epoch_seconds, 86400);
    const secs_of_day: i64 = @mod(epoch_seconds, 86400);

    const z = days + 719468;
    const era = @divFloor(if (z >= 0) z else z - 146096, 146097);
    const doe = z - era * 146097;
    const yoe = @divFloor(doe - @divFloor(doe, 1460) + @divFloor(doe, 36524) - @divFloor(doe, 146096), 365);
    var y = yoe + era * 400;
    const doy = doe - (365 * yoe + @divFloor(yoe, 4) - @divFloor(yoe, 100));
    const mp = @divFloor(5 * doy + 2, 153);
    const d = doy - @divFloor(153 * mp + 2, 5) + 1;
    var m: i64 = mp + (if (mp < 10) @as(i64, 3) else @as(i64, -9));
    y += if (m <= 2) 1 else 0;

    const hh = @divFloor(secs_of_day, 3600);
    const mm = @divFloor(@mod(secs_of_day, 3600), 60);
    const ss = @mod(secs_of_day, 60);

    if (m <= 0) m += 12;

    return std.fmt.allocPrint(allocator, "{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}Z", .{
        @as(u64, @intCast(y)),
        @as(u64, @intCast(m)),
        @as(u64, @intCast(d)),
        @as(u64, @intCast(hh)),
        @as(u64, @intCast(mm)),
        @as(u64, @intCast(ss)),
    });
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
