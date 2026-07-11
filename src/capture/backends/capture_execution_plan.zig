const std = @import("std");
const backend_contract = @import("capture_backend_contract.zig");
const portal_screenshot = @import("portal_screenshot.zig");
const c = @cImport({
    @cInclude("runtime/env.h");
    @cInclude("runtime/tool_lookup.h");
});

fn envValue(environ: std.process.Environ, key: []const u8) ?[*:0]const u8 {
    const value = environ.getPosix(key) orelse return null;
    return value.ptr;
}

fn envTrimmed(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    var result: c.ShaulaEnvSpan = .{ .data = null, .length = 0 };
    if (c.shaula_env_value_trimmed(envValue(environ, key), &result) != c.SHAULA_ENV_STATUS_VALID) {
        return null;
    }
    return result.data[0..result.length];
}

fn grimPath() ?[]const u8 {
    var result: c.ShaulaRuntimeToolSpan = .{ .data = null, .length = 0 };
    if (c.shaula_runtime_tool_grim_path(&result) != c.SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK) {
        return null;
    }
    return result.data[0..result.length];
}

pub const Options = struct {
    backend_label: []const u8,
    mode_string: []const u8,
    operation: Operation,
    area_geometry: ?[]const u8,
    focused_output_name: ?[]const u8,
    output_path: []const u8,
};

pub const Operation = enum {
    area,
    current_output,
    all_outputs,
    window,
};

pub const Plan = struct {
    argv_storage: [9][]const u8 = undefined,
    argv_len: usize = 0,
    owned_helper_path: ?[]u8 = null,
    pub fn argv(self: *const Plan) []const []const u8 {
        return self.argv_storage[0..self.argv_len];
    }

    pub fn deinit(self: *Plan, allocator: std.mem.Allocator) void {
        if (self.owned_helper_path) |path| allocator.free(path);
        self.* = .{};
    }
};

/// Resolve the concrete runtime capture command before process execution.
///
/// Contract constraints: this Module owns helper/grim argv shape, all-output vs
/// current-output command mapping from already-resolved inputs; compositor
/// probing stays in `compositor/focused_output.{c,h}` so deterministic `ERR_*`
/// mapping remains at the backend seam.
pub fn resolve(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: Options,
) !Plan {
    if (configuredRuntimeCaptureHelper(environ)) |helper_path| {
        return helperPlan(helper_path, options);
    }

    if (backend_contract.labelIsPortal(options.backend_label)) {
        const helper_path = portal_screenshot.resolveHelperBinary(allocator, io, environ) catch return error.BackendUnavailable;
        var plan = staticPlan(&.{
            helper_path,
            "--backend",
            options.backend_label,
            "--mode",
            options.mode_string,
            "--output",
            options.output_path,
        });
        plan.owned_helper_path = helper_path;
        return plan;
    }

    const grim_path = grimPath() orelse return error.BackendUnavailable;
    return switch (options.operation) {
        .area => blk: {
            const geometry = options.area_geometry orelse return error.BackendUnavailable;
            break :blk staticPlan(&.{ grim_path, "-g", geometry, options.output_path });
        },
        .current_output => blk: {
            const focused_output = options.focused_output_name orelse return error.BackendUnavailable;
            const plan = staticPlan(&.{ grim_path, "-o", focused_output, options.output_path });
            break :blk plan;
        },
        .all_outputs, .window => staticPlan(&.{ grim_path, options.output_path }),
    };
}

fn helperPlan(helper_path: []const u8, options: Options) Plan {
    if (options.area_geometry) |region| {
        return staticPlan(&.{
            helper_path,
            "--backend",
            options.backend_label,
            "--mode",
            options.mode_string,
            "--geometry",
            region,
            "--output",
            options.output_path,
        });
    }

    return staticPlan(&.{
        helper_path,
        "--backend",
        options.backend_label,
        "--mode",
        options.mode_string,
        "--output",
        options.output_path,
    });
}

fn staticPlan(argv: []const []const u8) Plan {
    var plan: Plan = .{};
    std.debug.assert(argv.len <= plan.argv_storage.len);
    for (argv, 0..) |arg, index| {
        plan.argv_storage[index] = arg;
    }
    plan.argv_len = argv.len;
    return plan;
}

fn configuredRuntimeCaptureHelper(environ: std.process.Environ) ?[]const u8 {
    if (envTrimmed(environ, "SHAULA_RUNTIME_CAPTURE_HELPER")) |helper_path| {
        return helper_path;
    }

    return null;
}
