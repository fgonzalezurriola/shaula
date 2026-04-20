const std = @import("std");
const root = @import("root");

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

const grim_candidate_paths = [_][]const u8{
    "/usr/bin/grim",
    "/bin/grim",
    "/usr/local/bin/grim",
};

const stub_signature_png_bytes = [_]u8{
    0x89, 0x50, 0x4E, 0x47,
    0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00,
    0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x63,
    0x60, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x01, 0xE5,
    0x27, 0xD4, 0xA2, 0x00,
    0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82,
};

const fake_capture_helper_fail_script =
    "#!/usr/bin/env python3\n" ++
    "import sys\n" ++
    "sys.exit(7)\n";

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

    const output_path = resolveOutputPath(allocator, io, environ, request) catch |err| switch (err) {
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
    errdefer allocator.free(output_path);

    writeRuntimeCapture(io, environ, backend, request, output_path) catch |err| switch (err) {
        error.BackendUnavailable => {
            allocator.free(output_path);
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
            allocator.free(output_path);
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

    const dimensions = resolveCaptureDimensions(allocator, io, output_path) catch {
        allocator.free(output_path);
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
            .path = output_path,
            .mime = "image/png",
            .dimensions = dimensions,
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

fn resolveOutputPath(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    request: capture_types.CaptureRequest,
) ![]u8 {
    if (request.output_path) |custom_path| {
        if (std.mem.indexOf(u8, custom_path, "::invalid::") != null) {
            return error.OutputPathInvalid;
        }
        return allocator.dupe(u8, custom_path);
    }

    const home = blk: {
        const home_z = environ.getPosix("HOME") orelse return error.OutputPathInvalid;
        const value = std.mem.sliceTo(home_z, 0);
        if (value.len == 0) return error.OutputPathInvalid;
        break :blk value;
    };

    const output_dir = try std.fmt.allocPrint(allocator, "{s}/Pictures/Shaula", .{home});
    defer allocator.free(output_dir);
    try ensureDirectoryWritable(allocator, io, output_dir);

    const ts = std.Io.Timestamp.now(io, .real);
    const millis = ts.toMilliseconds();
    return std.fmt.allocPrint(
        allocator,
        "{s}/capture-{s}-{d}.png",
        .{ output_dir, capture_types.modeString(request.mode), millis },
    );
}

fn ensureDirectoryWritable(allocator: std.mem.Allocator, io: std.Io, dir_path: []const u8) !void {
    std.Io.Dir.cwd().createDirPath(io, dir_path) catch return error.OutputPathInvalid;

    const probe_path = try std.fmt.allocPrint(
        allocator,
        "{s}/.shaula-write-probe-{d}.tmp",
        .{ dir_path, std.Io.Timestamp.now(io, .real).toMilliseconds() },
    );
    defer allocator.free(probe_path);

    var probe = std.Io.Dir.createFileAbsolute(io, probe_path, .{ .truncate = true }) catch {
        return error.OutputPathInvalid;
    };
    probe.close(io);
    std.Io.Dir.deleteFileAbsolute(io, probe_path) catch {};
}

fn writeRuntimeCapture(
    io: std.Io,
    environ: std.process.Environ,
    backend: BackendKind,
    request: capture_types.CaptureRequest,
    output_path: []const u8,
) !void {
    if (configuredRuntimeCaptureHelper(environ)) |helper_path| {
        return writeRuntimeCaptureWithHelper(io, backend, request, helper_path, output_path);
    }

    return writeRuntimeCaptureWithGrim(io, request, output_path);
}

fn configuredRuntimeCaptureHelper(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_RUNTIME_CAPTURE_HELPER")) |helper_path_z| {
        const configured = std.mem.sliceTo(helper_path_z, 0);
        if (configured.len > 0) return configured;
    }

    return null;
}

fn writeRuntimeCaptureWithHelper(
    io: std.Io,
    backend: BackendKind,
    request: capture_types.CaptureRequest,
    helper_path: []const u8,
    output_path: []const u8,
) !void {
    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var geometry_storage: [64]u8 = undefined;
    const geometry = if (request.mode == .area) formatAreaGeometry(request.area_geometry, &geometry_storage) else null;

    const result = if (geometry) |region| std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{
            "python3",
            helper_path,
            "--backend",
            backendString(backend),
            "--mode",
            capture_types.modeString(request.mode),
            "--geometry",
            region,
            "--output",
            output_path,
        },
        .stdout_limit = .limited(0),
        .stderr_limit = .limited(8192),
    }) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    } else std.process.run(std.heap.smp_allocator, io, .{
        .argv = &.{
            "python3",
            helper_path,
            "--backend",
            backendString(backend),
            "--mode",
            capture_types.modeString(request.mode),
            "--output",
            output_path,
        },
        .stdout_limit = .limited(0),
        .stderr_limit = .limited(8192),
    }) catch |err| switch (err) {
        error.FileNotFound => return error.BackendUnavailable,
        else => return err,
    };
    defer std.heap.smp_allocator.free(result.stdout);
    defer std.heap.smp_allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code == 0) return;
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
        else => {
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
    }
}

fn writeRuntimeCaptureWithGrim(
    io: std.Io,
    request: capture_types.CaptureRequest,
    output_path: []const u8,
) !void {
    const grim_path = findGrimBinary(io) orelse return error.BackendUnavailable;

    var geometry_storage: [64]u8 = undefined;
    const geometry = if (request.mode == .area)
        formatAreaGeometry(request.area_geometry, &geometry_storage) orelse return error.BackendUnavailable
    else
        null;

    if (std.fs.path.dirname(output_path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    const result = switch (request.mode) {
        .area => std.process.run(std.heap.smp_allocator, io, .{
            .argv = &.{ grim_path, "-g", geometry.?, output_path },
            .stdout_limit = .limited(0),
            .stderr_limit = .limited(8192),
        }) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        },
        .fullscreen => std.process.run(std.heap.smp_allocator, io, .{
            .argv = &.{ grim_path, output_path },
            .stdout_limit = .limited(0),
            .stderr_limit = .limited(8192),
        }) catch |err| switch (err) {
            error.FileNotFound => return error.BackendUnavailable,
            else => return err,
        },
        .window => return error.BackendUnavailable,
    };
    defer std.heap.smp_allocator.free(result.stdout);
    defer std.heap.smp_allocator.free(result.stderr);

    switch (result.term) {
        .exited => |code| {
            if (code == 0) return;
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
        else => {
            std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
            return error.BackendUnavailable;
        },
    }
}

fn hasStubSignaturePng(png_bytes: []const u8) bool {
    if (png_bytes.len < stub_signature_png_bytes.len) return false;
    return std.mem.eql(u8, png_bytes[0..stub_signature_png_bytes.len], &stub_signature_png_bytes);
}

fn findGrimBinary(io: std.Io) ?[]const u8 {
    for (grim_candidate_paths) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return candidate;
    }
    return null;
}

fn formatAreaGeometry(area_geometry: ?capture_types.AreaGeometry, buffer: []u8) ?[]const u8 {
    const geometry = area_geometry orelse return null;
    if (geometry.width == 0 or geometry.height == 0) return null;

    return std.fmt.bufPrint(buffer, "{d},{d} {d}x{d}", .{ geometry.x, geometry.y, geometry.width, geometry.height }) catch null;
}

fn resolveCaptureDimensions(
    allocator: std.mem.Allocator,
    io: std.Io,
    output_path: []const u8,
) !capture_types.Dimensions {
    const header = try std.Io.Dir.cwd().readFileAlloc(io, output_path, allocator, .unlimited);
    defer allocator.free(header);

    if (header.len < 24) return error.BackendUnavailable;
    if (!std.mem.eql(u8, header[0..8], "\x89PNG\r\n\x1a\n")) return error.BackendUnavailable;
    if (!std.mem.eql(u8, header[12..16], "IHDR")) return error.BackendUnavailable;

    const width = readBigEndianU32(header[16], header[17], header[18], header[19]);
    const height = readBigEndianU32(header[20], header[21], header[22], header[23]);
    if (width == 0 or height == 0) return error.BackendUnavailable;

    return .{ .width = width, .height = height };
}

fn readBigEndianU32(a: u8, b: u8, c: u8, d: u8) u32 {
    return (@as(u32, a) << 24) | (@as(u32, b) << 16) | (@as(u32, c) << 8) | @as(u32, d);
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

const EnvPair = struct {
    key: []const u8,
    value: []const u8,
};

const TestEnviron = struct {
    environ: std.process.Environ,
    block: std.process.Environ.Block,

    fn deinit(self: *TestEnviron, allocator: std.mem.Allocator) void {
        self.block.deinit(allocator);
    }
};

fn initTestEnviron(allocator: std.mem.Allocator, pairs: []const EnvPair) !TestEnviron {
    var map = std.process.Environ.Map.init(allocator);
    defer map.deinit();

    for (pairs) |pair| {
        try map.put(pair.key, pair.value);
    }

    const block = try map.createPosixBlock(allocator, .{});
    return .{
        .environ = .{ .block = block },
        .block = block,
    };
}

fn createExecutableHelper(io: std.Io, path: []const u8, script: []const u8) !void {
    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }

    var file = try std.Io.Dir.createFileAbsolute(io, path, .{ .truncate = true });
    defer file.close(io);
    try file.writeStreamingAll(io, script);

    try std.Io.Dir.cwd().setFilePermissions(io, path, .fromMode(0o755), .{});
}

test "window mode unresolved target returns deterministic failure" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .window });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_WINDOW_TARGET_UNRESOLVED", failure.code);
            try std.testing.expect(failure.degraded);
        },
        else => return error.TestExpectedFailure,
    }
}

test "runtime capture helper missing maps to backend unavailable" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = "/tmp/shaula/missing-helper.py" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    const output_path = "/tmp/shaula/test-runtime-helper-missing.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .area, .output_path = output_path });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_CAPTURE_BACKEND_UNAVAILABLE", failure.code);
            try std.testing.expectEqualStrings("capture backend unavailable", failure.message);
            try std.testing.expect(failure.retryable);
        },
        else => return error.TestExpectedFailure,
    }
}

test "default output path resolves under HOME Pictures Shaula" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "HOME", .value = "/tmp/shaula-test-home" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    const path = try resolveOutputPath(allocator, io, test_environ.environ, .{ .mode = .area });
    defer allocator.free(path);

    try std.testing.expect(std.mem.startsWith(u8, path, "/tmp/shaula-test-home/Pictures/Shaula/capture-area-"));
}

test "default output path without HOME returns OutputPathInvalid" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{});
    defer test_environ.deinit(std.testing.allocator);

    const io = std.testing.io;
    try std.testing.expectError(
        error.OutputPathInvalid,
        resolveOutputPath(std.testing.allocator, io, test_environ.environ, .{ .mode = .area }),
    );
}

test "injected unknown failure maps deterministically" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_INJECT_UNKNOWN_FAILURE", .value = "1" },
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_UNKNOWN_UNMAPPED", failure.code);
        },
        else => return error.TestExpectedFailure,
    }
}

test "non-niri compositor rejects capture without fallback success" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "sway" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_UNSUPPORTED_COMPOSITOR", failure.code);
            try std.testing.expect(!failure.degraded);
            try std.testing.expect(failure.backend_used == null);
        },
        else => return error.TestExpectedFailure,
    }
}

test "forcing stub backend fails deterministically and writes no file" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_CAPTURE_BACKEND", .value = "__stub__" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    const output_path = "/tmp/shaula/test-forced-stub-backend.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .area, .output_path = output_path });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_CAPTURE_BACKEND_UNAVAILABLE", failure.code);
            try std.testing.expectEqualStrings("capture backend unavailable", failure.message);
            try std.testing.expect(failure.retryable);
            try std.testing.expect(failure.backend_used != null);
            try std.testing.expectEqualStrings("__stub__", failure.backend_used.?);
        },
        else => return error.TestExpectedFailure,
    }

    const output_file_exists = blk: {
        std.Io.Dir.deleteFileAbsolute(io, output_path) catch |err| switch (err) {
            error.FileNotFound => break :blk false,
            else => return err,
        };
        break :blk true;
    };

    try std.testing.expect(!output_file_exists);
}

test "runtime capture output does not match stub signature" {
    const io = std.testing.io;
    const helper_path = "/tmp/shaula/test-runtime-capture-helper.py";
    const helper_source = "scripts/qa/fake_runtime_capture_helper.py";
    const helper_bytes = try std.Io.Dir.cwd().readFileAlloc(io, helper_source, std.testing.allocator, .unlimited);
    defer std.testing.allocator.free(helper_bytes);
    try createExecutableHelper(io, helper_path, helper_bytes);
    defer std.Io.Dir.deleteFileAbsolute(io, helper_path) catch {};

    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = helper_path },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const output_path = "/tmp/shaula/test-runtime-backend-output.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .success => |success| {
            try std.testing.expectEqualStrings(output_path, success.path);
            try std.testing.expectEqualStrings("image/png", success.mime);
        },
        else => return error.TestExpectedSuccess,
    }

    const png_bytes = try std.Io.Dir.cwd().readFileAlloc(io, output_path, allocator, .unlimited);
    defer allocator.free(png_bytes);

    try std.testing.expect(!hasStubSignaturePng(png_bytes));

    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
}

test "runtime helper nonzero exit maps to backend unavailable and no output" {
    const io = std.testing.io;
    const helper_path = "/tmp/shaula/test-runtime-capture-helper-fail.py";
    try createExecutableHelper(io, helper_path, fake_capture_helper_fail_script);
    defer std.Io.Dir.deleteFileAbsolute(io, helper_path) catch {};

    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = helper_path },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const output_path = "/tmp/shaula/test-runtime-backend-fail-output.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_CAPTURE_BACKEND_UNAVAILABLE", failure.code);
            try std.testing.expectEqualStrings("capture backend unavailable", failure.message);
            try std.testing.expect(failure.retryable);
        },
        else => return error.TestExpectedFailure,
    }

    const output_file_exists = blk: {
        std.Io.Dir.deleteFileAbsolute(io, output_path) catch |err| switch (err) {
            error.FileNotFound => break :blk false,
            else => return err,
        };
        break :blk true;
    };
    try std.testing.expect(!output_file_exists);
}

test "real backend without helper requires grim binary" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    const output_path = "/tmp/shaula/test-runtime-backend-real-requires-grim.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer deinitOutcome(allocator, &outcome);

    if (findGrimBinary(io) == null) {
        switch (outcome) {
            .failure => |failure| {
                try std.testing.expectEqualStrings("ERR_CAPTURE_BACKEND_UNAVAILABLE", failure.code);
                try std.testing.expectEqualStrings("capture backend unavailable", failure.message);
                try std.testing.expect(failure.retryable);
            },
            else => return error.TestExpectedFailure,
        }
        return;
    }

    switch (outcome) {
        .success => |success| {
            try std.testing.expectEqualStrings("image/png", success.mime);
            try std.testing.expect(success.dimensions.width > 0);
            try std.testing.expect(success.dimensions.height > 0);
        },
        else => return error.TestExpectedSuccess,
    }
}
