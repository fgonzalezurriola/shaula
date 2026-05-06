const std = @import("std");
const root = @import("root");

const runtime_exec = @import("capture_backend_runtime_exec.zig");
const output_path = @import("capture_backend_output_path.zig");
const png_meta = @import("capture_backend_png_meta.zig");
const failure = @import("capture_backend_failure.zig");

const standalone_capture_types = struct {
    pub const CaptureMode = enum {
        area,
        fullscreen,
        focused,
        window,
    };

    pub const AreaGeometry = struct {
        x: i32,
        y: i32,
        width: u32,
        height: u32,
    };

    pub const CaptureRequest = struct {
        mode: CaptureMode,
        output_path: ?[]const u8 = null,
        window_id: ?[]const u8 = null,
        area_geometry: ?AreaGeometry = null,
    };

    pub const Dimensions = struct {
        width: u32,
        height: u32,
    };

    pub const CaptureSuccess = struct {
        mode: CaptureMode,
        path: []const u8,
        mime: []const u8,
        dimensions: Dimensions,
        backend_used: []const u8,
        latency_ms: u32,
        degraded: bool,
    };

    pub const CaptureFailure = struct {
        mode: CaptureMode,
        code: []const u8,
        message: []const u8,
        retryable: bool,
        degraded: bool,
        backend_used: ?[]const u8,
    };

    pub const CaptureOutcome = union(enum) {
        success: CaptureSuccess,
        failure: CaptureFailure,
    };

    pub fn formatAreaGeometryArg(area_geometry: ?AreaGeometry, buffer: []u8) ?[]const u8 {
        const geometry = area_geometry orelse return null;
        if (geometry.width == 0 or geometry.height == 0) return null;

        return std.fmt.bufPrint(buffer, "{d},{d} {d}x{d}", .{ geometry.x, geometry.y, geometry.width, geometry.height }) catch null;
    }

    pub fn modeString(mode: CaptureMode) []const u8 {
        return switch (mode) {
            .area => "area",
            .fullscreen => "fullscreen",
            .focused => "focused",
            .window => "window",
        };
    }
};

const standalone_preflight_probe = struct {
    pub fn detectCompositor(environ: std.process.Environ) []const u8 {
        if (environ.getPosix("SHAULA_COMPOSITOR")) |value| {
            const explicit = std.mem.sliceTo(value, 0);
            if (std.ascii.eqlIgnoreCase(explicit, "niri")) return "niri";
            return "unsupported";
        }

        if (environ.getPosix("NIRI_SOCKET") != null) {
            return "niri";
        }

        return "unsupported";
    }
};

const capture_types = if (@hasDecl(root, "capture_types_module"))
    root.capture_types_module
else
    standalone_capture_types;

const preflight = if (@hasDecl(root, "preflight_probe_module"))
    root.preflight_probe_module
else
    standalone_preflight_probe;

const standalone_runtime_capabilities = struct {
    const Self = @This();

    pub const BackendKind = enum {
        niri_wayland_direct,
        portal_screenshot,
        stub,
    };

    pub fn resolveBackend(environ: std.process.Environ) Self.BackendKind {
        if (environ.getPosix("SHAULA_CAPTURE_BACKEND")) |value| {
            const token = std.mem.sliceTo(value, 0);
            if (std.mem.eql(u8, token, "__stub__")) {
                return .stub;
            }
        }

        if (environ.getPosix("SHAULA_CAPTURE_FORCE_PORTAL")) |value| {
            const token = std.mem.sliceTo(value, 0);
            if (std.mem.eql(u8, token, "1") or std.ascii.eqlIgnoreCase(token, "true")) {
                return .portal_screenshot;
            }
        }

        const compositor = preflight.detectCompositor(environ);
        if (std.mem.eql(u8, compositor, "niri")) {
            return .niri_wayland_direct;
        }

        return .portal_screenshot;
    }

    pub fn backendLabel(kind: Self.BackendKind) []const u8 {
        return switch (kind) {
            .niri_wayland_direct => "niri-wayland-direct",
            .portal_screenshot => "portal-screenshot",
            .stub => "__stub__",
        };
    }
};

const runtime_capabilities = if (@hasDecl(root, "runtime_capabilities_module"))
    root.runtime_capabilities_module
else
    standalone_runtime_capabilities;

pub const BackendKind = runtime_capabilities.BackendKind;

/// Execute a single backend capture request and return deterministic outcome.
///
/// This function is a core runtime boundary. It must preserve taxonomy stability
/// (`ERR_*`) and JSON contract expectations used by QA, history persistence, and
/// release-readiness checks.
pub fn execute(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    request: capture_types.CaptureRequest,
) !capture_types.CaptureOutcome {
    if (environ.getPosix("SHAULA_INJECT_UNKNOWN_FAILURE") != null) {
        return failure.unknown(capture_types, request.mode, null, "injected unknown failure");
    }

    const compositor = preflight.detectCompositor(environ);
    if (!std.mem.eql(u8, compositor, "niri")) {
        return failure.outcome(capture_types, request.mode, "ERR_UNSUPPORTED_COMPOSITOR", "unsupported compositor for shaula v1", false, false, null);
    }

    const backend = resolveBackend(environ);
    const backend_used = backendString(backend);
    const degraded_backend = backend == .portal_screenshot;

    if (backend == .stub) {
        return failure.backendUnavailable(capture_types, request.mode, backend_used);
    }

    if (request.mode == .window and resolveWindowTarget(request, environ) == null) {
        return failure.outcome(capture_types, .window, "ERR_WINDOW_TARGET_UNRESOLVED", "window target could not be resolved", false, true, backend_used);
    }

    const mode_string = capture_types.modeString(request.mode);
    const resolved_output_path = output_path.resolveOutputPath(
        allocator,
        io,
        mode_string,
        environ,
        request.output_path,
    ) catch |err| switch (err) {
        error.OutputPathInvalid => {
            return failure.outcome(capture_types, request.mode, "ERR_OUTPUT_PATH_INVALID", "output path is not writable", false, false, backend_used);
        },
        else => {
            return failure.unknown(capture_types, request.mode, backend_used, "capture backend failed with unmapped error");
        },
    };
    errdefer allocator.free(resolved_output_path);

    var geometry_storage: [64]u8 = undefined;
    const area_geometry = if (request.mode == .area)
        capture_types.formatAreaGeometryArg(request.area_geometry, &geometry_storage)
    else
        null;

    runtime_exec.writeRuntimeCapture(
        io,
        environ,
        backend_used,
        mode_string,
        request.mode == .area,
        request.mode == .focused,
        area_geometry,
        resolved_output_path,
    ) catch |err| switch (err) {
        error.BackendUnavailable => {
            allocator.free(resolved_output_path);
            return failure.backendUnavailable(capture_types, request.mode, backend_used);
        },
        else => {
            allocator.free(resolved_output_path);
            return failure.unknown(capture_types, request.mode, backend_used, "capture backend failed with unmapped error");
        },
    };

    const dimensions_meta = png_meta.resolveCaptureDimensions(allocator, io, resolved_output_path) catch {
        allocator.free(resolved_output_path);
        return failure.backendUnavailable(capture_types, request.mode, backend_used);
    };
    const latency_ms = defaultLatencyMs(request.mode, degraded_backend);

    return .{
        .success = .{
            .mode = request.mode,
            .path = resolved_output_path,
            .mime = "image/png",
            .dimensions = .{ .width = dimensions_meta.width, .height = dimensions_meta.height },
            .backend_used = backend_used,
            .latency_ms = latency_ms,
            .degraded = degraded_backend,
        },
    };
}

pub fn deinitOutcome(allocator: std.mem.Allocator, outcome: *capture_types.CaptureOutcome) void {
    switch (outcome.*) {
        .success => |success| allocator.free(success.path),
        .failure => {},
    }
}

fn resolveBackend(environ: std.process.Environ) BackendKind {
    return runtime_capabilities.resolveBackend(environ);
}

fn backendString(kind: BackendKind) []const u8 {
    return runtime_capabilities.backendLabel(kind);
}

fn resolveWindowTarget(request: capture_types.CaptureRequest, environ: std.process.Environ) ?[]const u8 {
    if (request.window_id) |window_id| {
        if (window_id.len > 0) return window_id;
    }

    if (environ.getPosix("SHAULA_WINDOW_ID")) |window_id_z| {
        const window_id = std.mem.sliceTo(window_id_z, 0);
        if (window_id.len > 0) return window_id;
    }

    if (environ.getPosix("SHAULA_WINDOW_TARGET_RESOLVED")) |resolved_z| {
        const resolved = std.mem.sliceTo(resolved_z, 0);
        if (std.mem.eql(u8, resolved, "1") or std.ascii.eqlIgnoreCase(resolved, "true")) {
            return "active-window";
        }
    }

    return null;
}

fn defaultLatencyMs(mode: capture_types.CaptureMode, degraded_backend: bool) u32 {
    const base: u32 = switch (mode) {
        .area => 12,
        .fullscreen => 16,
        .focused => 14,
        .window => 20,
    };

    if (degraded_backend) return base + 6;
    return base;
}
