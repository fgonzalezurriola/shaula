const std = @import("std");
const root = @import("root");
const backend_contract = @import("../backends/capture_backend_contract.zig");
const cli_json = @import("../cli/json.zig");
const compositor_runtime = @import("../compositor/runtime.zig");

const standalone_protocol = struct {
    pub const contract_version = "1.0.0";
    pub const ipc_version = "1.0.0";
};

const standalone_preflight = struct {
    pub fn detectCompositor(environ: std.process.Environ) []const u8 {
        return compositor_runtime.detect(environ).label;
    }
};

const standalone_recovery_policy = struct {
    pub fn exitCodeFor(code: []const u8) u8 {
        if (std.mem.eql(u8, code, "ERR_UNSUPPORTED_COMPOSITOR")) return 10;
        return 99;
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

const recovery_policy = if (@hasDecl(root, "recovery_policy_module"))
    root.recovery_policy_module
else
    standalone_recovery_policy;

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !u8 {
    const runtime = runtime_capabilities.resolve(allocator, io, environ);
    if (!runtime.compositor_supported) {
        try writeErrorJson(io, "capabilities list", "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, runtime.compositor.label);
        return recovery_policy.exitCodeFor("ERR_UNSUPPORTED_COMPOSITOR");
    }

    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const backend = runtime.backendUsedLabel();
    const fallbacks = runtime_capabilities.fallbacksFor(runtime.backend);
    const fallbacks_json = try stringArrayJson(allocator, fallbacks);
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
    const warnings = try cli_json.warningsAlloc(allocator, warning_values[0..warning_count]);
    defer allocator.free(warnings);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":\"capabilities list\",\"timestamp\":\"{s}\",\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"all_screens\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"portal_window_capable\":{s},\"result\":{{\"capture\":{{\"area\":{s},\"fullscreen\":{s},\"all_screens\":{s},\"window\":{s}}},\"backend\":\"{s}\",\"fallbacks\":{s},\"compositor\":\"{s}\",\"ipc_version\":\"{s}\",\"portal_available\":{s},\"portal_window_capable\":{s},\"overlay_supported\":{s}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
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

fn stringArrayJson(allocator: std.mem.Allocator, values: []const []const u8) ![]u8 {
    if (values.len == 0) {
        return allocator.dupe(u8, "[]");
    }

    var list = try std.ArrayList(u8).initCapacity(allocator, 32);
    defer list.deinit(allocator);

    try list.append(allocator, '[');
    for (values, 0..) |value, index| {
        if (index != 0) {
            try list.append(allocator, ',');
        }
        const encoded = try cli_json.stringAlloc(allocator, value);
        defer allocator.free(encoded);
        try list.appendSlice(allocator, encoded);
    }
    try list.append(allocator, ']');

    return list.toOwnedSlice(allocator);
}

fn writeErrorJson(io: std.Io, command: []const u8, code: []const u8, message: []const u8, retryable: bool, detected_compositor: []const u8) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const compositor_json = try cli_json.stringAlloc(allocator, detected_compositor);
    defer allocator.free(compositor_json);
    const details_json = try std.fmt.allocPrint(
        allocator,
        "{{\"detected_compositor\":{s}}}",
        .{compositor_json},
    );
    defer allocator.free(details_json);
    try cli_json.writeErrorWithDetails(io, command, code, message, retryable, details_json);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
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
