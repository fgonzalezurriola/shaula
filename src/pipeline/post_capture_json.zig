const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const capture_types = @import("../capture/types.zig");
const cli_json = @import("../cli/json.zig");
const types = @import("post_capture_types.zig");

/// Render the stable capture success JSON envelope.
///
/// Contract constraint: duplicated top-level/result fields, degraded action
/// fields, and partial-success rules live here so post-capture side effects
/// cannot drift the machine-readable contract.
pub fn write(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    flags: anytype,
    outcome: types.PostCaptureOutcome,
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

fn previewJsonField(allocator: std.mem.Allocator, result: types.PreviewPipelineResult) ![]u8 {
    const error_json = try errorJsonField(allocator, result.code, result.message);
    defer allocator.free(error_json);

    const action_json = if (result.action) |action|
        try cli_json.stringAlloc(allocator, action.asString())
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
