const std = @import("std");
const capture_backend = @import("capture_backend.zig");
const capture_types = @import("../capture/types.zig");

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

const GeometryFixtureCase = struct {
    name: []const u8,
    local: capture_types.AreaGeometry,
    output: capture_types.OutputLayout,
    expected: capture_types.AreaGeometry,
    expected_runtime_arg: []const u8,
};

fn readGeometryFixtureCases(allocator: std.mem.Allocator, path: []const u8) ![]GeometryFixtureCase {
    const io = std.testing.io;
    const raw = try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .unlimited);
    defer allocator.free(raw);

    const parsed = try std.json.parseFromSlice([]GeometryFixtureCase, allocator, raw, .{});
    defer parsed.deinit();

    const fixtures = try allocator.alloc(GeometryFixtureCase, parsed.value.len);
    errdefer allocator.free(fixtures);
    var initialized: usize = 0;
    errdefer {
        var i: usize = 0;
        while (i < initialized) : (i += 1) {
            allocator.free(fixtures[i].name);
            allocator.free(fixtures[i].expected_runtime_arg);
        }
    }

    for (parsed.value, 0..) |fixture, index| {
        fixtures[index] = .{
            .name = try allocator.dupe(u8, fixture.name),
            .local = fixture.local,
            .output = fixture.output,
            .expected = fixture.expected,
            .expected_runtime_arg = try allocator.dupe(u8, fixture.expected_runtime_arg),
        };
        initialized += 1;
    }

    return fixtures;
}

fn assertFixtureNormalization(case: GeometryFixtureCase) !void {
    const normalized = capture_types.normalizeOutputLocalGeometry(case.local, case.output);
    try std.testing.expectEqualDeep(case.expected, normalized);

    var geometry_storage: [64]u8 = undefined;
    const runtime_arg = capture_types.formatAreaGeometryArg(normalized, &geometry_storage) orelse return error.TestExpectedEqual;
    try std.testing.expectEqualStrings(case.expected_runtime_arg, runtime_arg);
}

test "task13 fixture multi-output geometry normalization and formatting round-trip" {
    const fixtures = try readGeometryFixtureCases(std.testing.allocator, "tests/fixtures/capture/task13_multioutput_geometry.json");
    defer {
        for (fixtures) |fixture| {
            std.testing.allocator.free(fixture.name);
            std.testing.allocator.free(fixture.expected_runtime_arg);
        }
        std.testing.allocator.free(fixtures);
    }

    for (fixtures) |fixture_case| {
        try assertFixtureNormalization(fixture_case);
    }
}

test "task13 fixture fractional scaling yields deterministic non-negative dimensions" {
    const fixtures = try readGeometryFixtureCases(std.testing.allocator, "tests/fixtures/capture/task13_fractional_scaling.json");
    defer {
        for (fixtures) |fixture| {
            std.testing.allocator.free(fixture.name);
            std.testing.allocator.free(fixture.expected_runtime_arg);
        }
        std.testing.allocator.free(fixtures);
    }

    for (fixtures) |fixture_case| {
        try assertFixtureNormalization(fixture_case);

        const normalized = capture_types.normalizeOutputLocalGeometry(fixture_case.local, fixture_case.output);
        try std.testing.expect(normalized.width > 0);
        try std.testing.expect(normalized.height > 0);
    }
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
    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .window });
    defer capture_backend.deinitOutcome(allocator, &outcome);

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
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = "/tmp/shaula/missing-helper" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    const output_path = "/tmp/shaula/test-runtime-helper-missing.png";
    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area, .output_path = output_path });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_CAPTURE_BACKEND_UNAVAILABLE", failure.code);
            try std.testing.expectEqualStrings("capture backend unavailable", failure.message);
            try std.testing.expect(failure.retryable);
        },
        else => return error.TestExpectedFailure,
    }
}

test "default output path resolves under HOME Pictures shaula" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "HOME", .value = "/tmp/shaula-test-home" },
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = "scripts/qa/fake_runtime_capture_helper.sh" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .success => |success| {
            try std.testing.expect(std.mem.startsWith(u8, success.path, "/tmp/shaula-test-home/Pictures/shaula/capture-area-"));
        },
        else => return error.TestExpectedSuccess,
    }
}

test "default output path falls back to HOME shaula when Pictures path is unusable" {
    const io = std.testing.io;
    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    const home = try std.fmt.allocPrint(std.testing.allocator, "/tmp/shaula-test-home-fallback-{d}", .{millis});
    defer std.testing.allocator.free(home);
    try std.Io.Dir.cwd().createDirPath(io, home);

    // Make ~/Pictures a file so ~/Pictures/shaula cannot be created.
    const pictures_path = try std.fmt.allocPrint(std.testing.allocator, "{s}/Pictures", .{home});
    defer std.testing.allocator.free(pictures_path);
    var pictures_file = try std.Io.Dir.createFileAbsolute(io, pictures_path, .{ .truncate = true });
    pictures_file.close(io);

    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "HOME", .value = home },
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = "scripts/qa/fake_runtime_capture_helper.sh" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .success => |success| {
            const expected_prefix = try std.fmt.allocPrint(std.testing.allocator, "{s}/shaula/capture-area-", .{home});
            defer std.testing.allocator.free(expected_prefix);
            try std.testing.expect(std.mem.startsWith(u8, success.path, expected_prefix));
        },
        else => return error.TestExpectedSuccess,
    }
}

test "default output path without HOME returns OutputPathInvalid" {
    var test_environ = try initTestEnviron(std.testing.allocator, &.{
        .{ .key = "SHAULA_COMPOSITOR", .value = "niri" },
        .{ .key = "SHAULA_RUNTIME_CAPTURE_HELPER", .value = "scripts/qa/fake_runtime_capture_helper.sh" },
    });
    defer test_environ.deinit(std.testing.allocator);

    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const io = std.testing.io;
    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .failure => |failure| {
            try std.testing.expectEqualStrings("ERR_OUTPUT_PATH_INVALID", failure.code);
        },
        else => return error.TestExpectedFailure,
    }
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
    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer capture_backend.deinitOutcome(allocator, &outcome);

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
    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area });
    defer capture_backend.deinitOutcome(allocator, &outcome);

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

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .area, .output_path = output_path });
    defer capture_backend.deinitOutcome(allocator, &outcome);

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
    const helper_path = "scripts/qa/fake_runtime_capture_helper.sh";

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

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    switch (outcome) {
        .success => |success| {
            try std.testing.expectEqualStrings(output_path, success.path);
            try std.testing.expectEqualStrings("image/png", success.mime);
        },
        else => return error.TestExpectedSuccess,
    }

    const png_bytes = try std.Io.Dir.cwd().readFileAlloc(io, output_path, allocator, .unlimited);
    defer allocator.free(png_bytes);

    const stub_signature = [_]u8{
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
    try std.testing.expect(!std.mem.eql(u8, png_bytes[0..stub_signature.len], &stub_signature));

    std.Io.Dir.deleteFileAbsolute(io, output_path) catch {};
}

test "runtime helper nonzero exit maps to backend unavailable and no output" {
    const io = std.testing.io;
    const helper_path = "/tmp/shaula/test-runtime-capture-helper-fail.sh";
    const fail_script =
        "#!/usr/bin/env bash\n" ++
        "exit 7\n";
    try createExecutableHelper(io, helper_path, fail_script);
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

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer capture_backend.deinitOutcome(allocator, &outcome);

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

    var outcome = try capture_backend.execute(allocator, io, test_environ.environ, .{ .mode = .fullscreen, .output_path = output_path });
    defer capture_backend.deinitOutcome(allocator, &outcome);

    if (std.Io.Dir.accessAbsolute(io, "/usr/bin/grim", .{}) == error.FileNotFound and
        std.Io.Dir.accessAbsolute(io, "/bin/grim", .{}) == error.FileNotFound and
        std.Io.Dir.accessAbsolute(io, "/usr/local/bin/grim", .{}) == error.FileNotFound)
    {
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
