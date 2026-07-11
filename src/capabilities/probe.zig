const std = @import("std");
const root = @import("root");
const backend_contract = @import("../capture/backends/capture_backend_contract.zig");
const compositor_runtime = @import("../compositor/runtime.zig");
const c = @cImport({
    @cInclude("cli/json.h");
    @cInclude("errors/taxonomy.h");
});

const standalone_protocol = struct {
    pub const ipc_version = "1.0.0";
};

const standalone_preflight = struct {
    pub fn detectCompositor(environ: std.process.Environ) []const u8 {
        return compositor_runtime.detect(environ).label;
    }
};

const recovery_policy = struct {
    pub fn exitCodeFor(code: []const u8) u8 {
        return c.shaula_error_exit_code_for(.{ .data = code.ptr, .length = code.len });
    }
};

const protocol = if (@hasDecl(root, "protocol_module"))
    root.protocol_module
else
    standalone_protocol;

const preflight = if (@hasDecl(root, "preflight_probe_module"))
    root.preflight_probe_module
else
    standalone_preflight;

const runtime_capabilities = if (@hasDecl(root, "runtime_capabilities_module"))
    root.runtime_capabilities_module
else
    @import("runtime.zig");

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const runtime = runtime_capabilities.resolve(allocator, io, environ);
    if (!runtime.compositor_supported) {
        try writeErrorJson(io, "capabilities list", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    const backend = runtime.backendUsedLabel();
    const fallbacks = runtime_capabilities.fallbacksFor(runtime.backend);
    const fallbacks_json = try jsonStringArrayAlloc(allocator, fallbacks);
    defer allocator.free(fallbacks_json);

    var warning_values: [2][]const u8 = undefined;
    var warning_count: usize = 0;
    if (!runtime.capture.window) {
        warning_values[warning_count] = backend_contract.warning_window_capture_degraded;
        warning_count += 1;
    }
    if (runtime.usesPortalBackend()) {
        warning_values[warning_count] = backend_contract.warning_portal_fallback;
        warning_count += 1;
    }
    const warnings = try jsonStringArrayAlloc(allocator, warning_values[0..warning_count]);
    defer allocator.free(warnings);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"capabilities list\",\"timestamp\":\"{s}\",\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"all_screens\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"portal_window_capable\":{s},\"result\":{{\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"all_screens\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"compositor\":\"{s}\",\"ipc_version\":\"{s}\",\"portal_available\":{s},\"portal_window_capable\":{s},\"overlay_supported\":{s}}},\"warnings\":{s}}}\n",
        .{
            jsonContractVersion(),
            ts,
            boolToJson(runtime.capture.area),
            boolToJson(runtime.capture.fullscreen),
            boolToJson(runtime.capture.all_screens),
            boolToJson(runtime.capture.window),
            backend,
            fallbacks_json,
            boolToJson(runtime.portal_window_capable),
            boolToJson(runtime.capture.area),
            boolToJson(runtime.capture.fullscreen),
            boolToJson(runtime.capture.all_screens),
            boolToJson(runtime.capture.window),
            backend,
            fallbacks_json,
            runtime.compositor.label,
            protocol.ipc_version,
            boolToJson(runtime.portal_available),
            boolToJson(runtime.portal_window_capable),
            boolToJson(runtime.overlay_supported),
            warnings,
        },
    );
    try stdout.interface.flush();
    return 0;
}

fn boolToJson(value: bool) []const u8 {
    return if (value) "true" else "false";
}

fn jsonSpan(value: []const u8) c.ShaulaJsonSpan {
    return .{ .data = value.ptr, .length = value.len };
}

fn jsonContractVersion() []const u8 {
    const value = c.shaula_json_contract_version();
    return value.data[0..value.length];
}

fn jsonTimestampAlloc(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_timestamp_from_unix_seconds(std.Io.Timestamp.now(io, .real).toSeconds(), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_string_escape(jsonSpan(value), &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn jsonStringArrayAlloc(allocator: std.mem.Allocator, values: []const []const u8) ![]u8 {
    const spans = try allocator.alloc(c.ShaulaJsonSpan, values.len);
    defer allocator.free(spans);
    for (values, 0..) |value, index| spans[index] = jsonSpan(value);

    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_warnings_serialize(if (spans.len == 0) null else spans.ptr, spans.len, &output);
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;
    return allocator.dupe(u8, output.data[0..output.length]);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool, detected_compositor: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const compositor_json = try jsonStringAlloc(allocator, detected_compositor);
    defer allocator.free(compositor_json);
    const details_json = try std.fmt.allocPrint(
        allocator,
        "{{\"detected_compositor\":{s}}}",
        .{compositor_json},
    );
    defer allocator.free(details_json);

    var output: c.ShaulaJsonOwnedBytes = .{ .data = null, .length = 0 };
    defer c.shaula_json_owned_bytes_clear(&output);
    const status = c.shaula_json_basic_error_build(
        std.Io.Timestamp.now(io, .real).toSeconds(),
        jsonSpan(command),
        jsonSpan(code),
        jsonSpan(message),
        @intFromBool(retryable),
        jsonSpan(details_json),
        &output,
    );
    if (status != c.SHAULA_JSON_STATUS_OK) return error.JsonEncodingFailed;

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.writeAll(output.data[0..output.length]);
    try stdout.interface.flush();
}

test "capabilities compositor guard is deterministic" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "sway");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    try std.testing.expectEqualStrings("sway", preflight.detectCompositor(environ));
}

test "runtime capabilities backend naming is canonical" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    const runtime = runtime_capabilities.resolve(std.testing.allocator, std.testing.io, environ);
    try std.testing.expectEqualStrings("niri-wayland-direct", runtime_capabilities.backendLabel(runtime.backend));
    try std.testing.expect(runtime.capture.all_screens);
    try std.testing.expect(!runtime.capture.window);
}
