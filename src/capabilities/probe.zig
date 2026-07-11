const std = @import("std");
const root = @import("root");
const backend_contract = @import("../capture/backends/capture_backend_contract.zig");
const c = @cImport({
    @cInclude("capabilities/runtime.h");
    @cInclude("cli/json.h");
    @cInclude("compositor/runtime.h");
    @cInclude("errors/taxonomy.h");
});

const standalone_protocol = struct {
    pub const ipc_version = "1.0.0";
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

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn capabilitySpan(value: c.ShaulaEnvSpan) []const u8 {
    if (value.length == 0) return "";
    return value.data[0..value.length];
}

fn capabilitiesEnvironment(environ: std.process.Environ) c.ShaulaCapabilitiesEnvironment {
    return .{
        .compositor = .{
            .shaula_compositor = envValue(environ, "SHAULA_COMPOSITOR"),
            .niri_socket = envValue(environ, "NIRI_SOCKET"),
            .xdg_current_desktop = envValue(environ, "XDG_CURRENT_DESKTOP"),
            .xdg_session_desktop = envValue(environ, "XDG_SESSION_DESKTOP"),
            .wayland_display = envValue(environ, "WAYLAND_DISPLAY"),
        },
        .capture_backend = envValue(environ, "SHAULA_CAPTURE_BACKEND"),
        .capture_force_portal = envValue(environ, "SHAULA_CAPTURE_FORCE_PORTAL"),
        .portal_available = envValue(environ, "SHAULA_PORTAL_AVAILABLE"),
        .portal_window_capable = envValue(environ, "SHAULA_PORTAL_WINDOW_CAPABLE"),
    };
}

fn resolveRuntime(environ: std.process.Environ) c.ShaulaRuntimeDecision {
    var environment = capabilitiesEnvironment(environ);
    var runtime: c.ShaulaRuntimeDecision = std.mem.zeroes(c.ShaulaRuntimeDecision);
    if (c.shaula_capabilities_resolve(&environment, &runtime) != c.SHAULA_CAPABILITIES_STATUS_OK) unreachable;
    return runtime;
}

fn backendLabel(backend: c.ShaulaBackendKind) []const u8 {
    return capabilitySpan(c.shaula_capabilities_backend_label(backend));
}

fn fallbacksFor(backend: c.ShaulaBackendKind, storage: *[1][]const u8) []const []const u8 {
    const count = c.shaula_capabilities_fallback_count(backend);
    if (count == 0) return &.{};
    if (count != 1) unreachable;
    const fallback = c.shaula_capabilities_fallback_at(backend, 0);
    if (fallback == c.SHAULA_BACKEND_KIND_INVALID) unreachable;
    storage[0] = backendLabel(fallback);
    return storage[0..1];
}

fn detectCompositorLabel(environ: std.process.Environ) []const u8 {
    var environment: c.ShaulaCompositorEnvironment = .{
        .shaula_compositor = envValue(environ, "SHAULA_COMPOSITOR"),
        .niri_socket = envValue(environ, "NIRI_SOCKET"),
        .xdg_current_desktop = envValue(environ, "XDG_CURRENT_DESKTOP"),
        .xdg_session_desktop = envValue(environ, "XDG_SESSION_DESKTOP"),
        .wayland_display = envValue(environ, "WAYLAND_DISPLAY"),
    };
    var detection: c.ShaulaCompositorDetection = std.mem.zeroes(c.ShaulaCompositorDetection);
    if (c.shaula_compositor_detect(&environment, &detection) != c.SHAULA_COMPOSITOR_STATUS_OK) {
        return "unsupported";
    }
    if (detection.label.length == 0) return "";
    return detection.label.data[0..detection.label.length];
}

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const runtime = resolveRuntime(environ);
    const compositor_label = capabilitySpan(runtime.compositor.label);
    if (runtime.compositor_supported == 0) {
        try writeErrorJson(io, "capabilities list", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, compositor_label);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const ts = try jsonTimestampAlloc(allocator, io);
    defer allocator.free(ts);

    const backend = backendLabel(runtime.backend);
    var fallback_storage: [1][]const u8 = undefined;
    const fallbacks = fallbacksFor(runtime.backend, &fallback_storage);
    const fallbacks_json = try jsonStringArrayAlloc(allocator, fallbacks);
    defer allocator.free(fallbacks_json);

    var warning_values: [2][]const u8 = undefined;
    var warning_count: usize = 0;
    if (runtime.capture.window == 0) {
        warning_values[warning_count] = backend_contract.warning_window_capture_degraded;
        warning_count += 1;
    }
    if (c.shaula_capabilities_uses_portal_backend(runtime) == 1) {
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
            boolToJson(runtime.capture.area != 0),
            boolToJson(runtime.capture.fullscreen != 0),
            boolToJson(runtime.capture.all_screens != 0),
            boolToJson(runtime.capture.window != 0),
            backend,
            fallbacks_json,
            boolToJson(runtime.portal_window_capable != 0),
            boolToJson(runtime.capture.area != 0),
            boolToJson(runtime.capture.fullscreen != 0),
            boolToJson(runtime.capture.all_screens != 0),
            boolToJson(runtime.capture.window != 0),
            backend,
            fallbacks_json,
            compositor_label,
            protocol.ipc_version,
            boolToJson(runtime.portal_available != 0),
            boolToJson(runtime.portal_window_capable != 0),
            boolToJson(runtime.overlay_supported != 0),
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

    try std.testing.expectEqualStrings("sway", detectCompositorLabel(environ));
}

test "runtime capabilities backend naming is canonical" {
    var map = std.process.Environ.Map.init(std.testing.allocator);
    defer map.deinit();

    try map.put("SHAULA_COMPOSITOR", "niri");
    const block = try map.createPosixBlock(std.testing.allocator, .{});
    defer block.deinit(std.testing.allocator);

    const environ: std.process.Environ = .{ .block = block };

    const runtime = resolveRuntime(environ);
    try std.testing.expectEqualStrings("niri-wayland-direct", backendLabel(runtime.backend));
    try std.testing.expect(runtime.capture.all_screens != 0);
    try std.testing.expect(runtime.capture.window == 0);
}
