const std = @import("std");

const protocol = @import("../ipc/protocol.zig");
const capture_types = @import("../capture/types.zig");
const history_store = @import("../history/store.zig");
const clipboard_service = @import("../clipboard/service.zig");

pub const PipelineFlags = struct {
    save: bool,
    copy: bool,
};

/// Emit capture JSON when save/copy are requested.
///
/// Contract constraints:
/// - never suppress backend success payload, instead report `saved`/`clipboard`
///   fields with deterministic `ERR_*` codes.
/// - keep JSON contract version and fields stable for QA assertions.
pub fn writeCapturePipelineJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    command: []const u8,
    reported_mode: []const u8,
    success: capture_types.CaptureSuccess,
    flags: PipelineFlags,
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
        const copy_result = clipboard_service.copyImage(io, environ, success.path) catch {
            clipboard_ok = false;
            clipboard_error_code = "ERR_UNKNOWN_UNMAPPED";
            clipboard_error_message = "clipboard copy failed with unmapped error";
            return writeCapturePipelineJsonWithResolvedFields(allocator, io, command, reported_mode, success, flags, ts, saved_ok, saved_error_code, saved_error_message, clipboard_ok, clipboard_error_code, clipboard_error_message);
        };
        clipboard_ok = copy_result.ok;
        clipboard_error_code = copy_result.code;
        clipboard_error_message = copy_result.message;
    }

    return writeCapturePipelineJsonWithResolvedFields(allocator, io, command, reported_mode, success, flags, ts, saved_ok, saved_error_code, saved_error_message, clipboard_ok, clipboard_error_code, clipboard_error_message);
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

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":{s},\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"degraded\":{s},\"saved\":{{\"ok\":{s},\"path\":{s},\"error\":{s}}},\"clipboard\":{{\"ok\":{s},\"error\":{s}}},\"partial\":{s},\"result\":{{\"mode\":{s},\"path\":{s},\"mime\":{s},\"dimensions\":{{\"width\":{d},\"height\":{d}}},\"backend_used\":{s},\"latency_ms\":{d},\"saved\":{{\"ok\":{s},\"path\":{s}}},\"clipboard\":{{\"ok\":{s}}}}},\"warnings\":[]}}\n",
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
            if (saved_ok) "true" else "false",
            path_json,
            saved_error_json,
            if (clipboard_ok) "true" else "false",
            clipboard_error_json,
            if (partial) "true" else "false",
            mode_json,
            path_json,
            mime_json,
            success.dimensions.width,
            success.dimensions.height,
            backend_json,
            success.latency_ms,
            if (saved_ok) "true" else "false",
            path_json,
            if (clipboard_ok) "true" else "false",
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

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, value, .{});
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
