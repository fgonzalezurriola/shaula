const std = @import("std");

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
    pub fn argv(self: *const Plan) []const []const u8 {
        return self.argv_storage[0..self.argv_len];
    }

    pub fn deinit(self: *Plan, allocator: std.mem.Allocator) void {
        _ = allocator;
        self.* = .{};
    }
};

/// Resolve the concrete runtime capture command before process execution.
///
/// Contract constraints: this Module owns helper/grim argv shape, all-output vs
/// current-output command mapping from already-resolved inputs; compositor
/// probing stays in `compositor/focused_output.zig` so deterministic `ERR_*`
/// mapping remains at the backend seam.
pub fn resolve(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: Options,
) !Plan {
    _ = allocator;
    if (configuredRuntimeCaptureHelper(environ)) |helper_path| {
        return helperPlan(helper_path, options);
    }

    const grim_path = findGrimBinary(io) orelse return error.BackendUnavailable;
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
    for (argv, 0..) |arg, index| {
        plan.argv_storage[index] = arg;
    }
    plan.argv_len = argv.len;
    return plan;
}

fn configuredRuntimeCaptureHelper(environ: std.process.Environ) ?[]const u8 {
    if (environ.getPosix("SHAULA_RUNTIME_CAPTURE_HELPER")) |helper_path_z| {
        const configured = std.mem.sliceTo(helper_path_z, 0);
        if (configured.len > 0) return configured;
    }

    return null;
}

fn findGrimBinary(io: std.Io) ?[]const u8 {
    const grim_candidate_paths = [_][]const u8{
        "/usr/bin/grim",
        "/bin/grim",
        "/usr/local/bin/grim",
    };

    for (grim_candidate_paths) |candidate| {
        std.Io.Dir.accessAbsolute(io, candidate, .{}) catch continue;
        return candidate;
    }
    return null;
}
