const std = @import("std");
const root = @import("root");

const runtime_exec = @import("capture_backend_runtime_exec.zig");
const output_path = @import("capture_backend_output_path.zig");
const png_meta = @import("capture_backend_png_meta.zig");

const standalone_capture_types = struct {
    pub const CaptureMode = enum {
        area,
        fullscreen,
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

    pub fn modeString(mode: CaptureMode) []const u8 {
        return switch (mode) {
            .area => "area",
            .fullscreen => "fullscreen",
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
        return .{
            .failure = .{
                .mode = request.mode,
                .code = "ERR_UNKNOWN_UNMAPPED",
                .message = "injected unknown failure",
                .retryable = false,
                .degraded = false,
                .backend_used = null,
            },
        };
    }

    const compositor = preflight.detectCompositor(environ);
    if (!std.mem.eql(u8, compositor, "niri")) {
        return .{
            .failure = .{
                .mode = request.mode,
                .code = "ERR_UNSUPPORTED_COMPOSITOR",
                .message = "unsupported compositor for shaula v1",
                .retryable = false,
                .degraded = false,
                .backend_used = null,
            },
        };
    }

    const backend = resolveBackend(environ);
    const backend_used = backendString(backend);
    const degraded_backend = backend == .portal_screenshot;

    if (backend == .stub) {
        return .{
            .failure = .{
                .mode = request.mode,
                .code = "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                .message = "capture backend unavailable",
                .retryable = true,
                .degraded = false,
                .backend_used = backend_used,
            },
        };
    }

    if (request.mode == .window and resolveWindowTarget(request, environ) == null) {
        return .{
            .failure = .{
                .mode = .window,
                .code = "ERR_WINDOW_TARGET_UNRESOLVED",
                .message = "window target could not be resolved",
                .retryable = false,
                .degraded = true,
                .backend_used = backend_used,
            },
        };
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
            return .{
                .failure = .{
                    .mode = request.mode,
                    .code = "ERR_OUTPUT_PATH_INVALID",
                    .message = "output path is not writable",
                    .retryable = false,
                    .degraded = false,
                    .backend_used = backend_used,
                },
            };
        },
        else => {
            return .{
                .failure = .{
                    .mode = request.mode,
                    .code = "ERR_UNKNOWN_UNMAPPED",
                    .message = "capture backend failed with unmapped error",
                    .retryable = false,
                    .degraded = false,
                    .backend_used = backend_used,
                },
            };
        },
    };
    errdefer allocator.free(resolved_output_path);

    var geometry_storage: [64]u8 = undefined;
    const area_geometry = if (request.mode == .area)
        formatAreaGeometry(request.area_geometry, &geometry_storage)
    else
        null;

    runtime_exec.writeRuntimeCapture(
        io,
        environ,
        backend_used,
        mode_string,
        request.mode == .area,
        area_geometry,
        resolved_output_path,
    ) catch |err| switch (err) {
        error.BackendUnavailable => {
            allocator.free(resolved_output_path);
            return .{
                .failure = .{
                    .mode = request.mode,
                    .code = "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                    .message = "capture backend unavailable",
                    .retryable = true,
                    .degraded = false,
                    .backend_used = backend_used,
                },
            };
        },
        else => {
            allocator.free(resolved_output_path);
            return .{
                .failure = .{
                    .mode = request.mode,
                    .code = "ERR_UNKNOWN_UNMAPPED",
                    .message = "capture backend failed with unmapped error",
                    .retryable = false,
                    .degraded = false,
                    .backend_used = backend_used,
                },
            };
        },
    };

    const dimensions_meta = png_meta.resolveCaptureDimensions(allocator, io, resolved_output_path) catch {
        allocator.free(resolved_output_path);
        return .{
            .failure = .{
                .mode = request.mode,
                .code = "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                .message = "capture backend unavailable",
                .retryable = true,
                .degraded = false,
                .backend_used = backend_used,
            },
        };
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

fn formatAreaGeometry(area_geometry: ?capture_types.AreaGeometry, buffer: []u8) ?[]const u8 {
    const geometry = area_geometry orelse return null;
    if (geometry.width == 0 or geometry.height == 0) return null;

    return std.fmt.bufPrint(buffer, "{d},{d} {d}x{d}", .{ geometry.x, geometry.y, geometry.width, geometry.height }) catch null;
}

fn defaultLatencyMs(mode: capture_types.CaptureMode, degraded_backend: bool) u32 {
    const base: u32 = switch (mode) {
        .area => 12,
        .fullscreen => 16,
        .window => 20,
    };

    if (degraded_backend) return base + 6;
    return base;
}
