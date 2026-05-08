const std = @import("std");

const process_exec = @import("../runtime/process_exec.zig");

pub const Options = struct {
    backend_label: []const u8,
    mode_string: []const u8,
    operation: Operation,
    area_geometry: ?[]const u8,
    output_path: []const u8,
};

pub const Operation = enum {
    area,
    current_output,
    all_outputs,
    window,
};

const NiriFocusedOutput = struct {
    name: []const u8,
};

pub const Plan = struct {
    argv_storage: [9][]const u8 = undefined,
    argv_len: usize = 0,
    owned_focused_output: ?[]u8 = null,

    pub fn argv(self: *const Plan) []const []const u8 {
        return self.argv_storage[0..self.argv_len];
    }

    pub fn deinit(self: *Plan, allocator: std.mem.Allocator) void {
        if (self.owned_focused_output) |name| allocator.free(name);
        self.* = .{};
    }
};

/// Resolve the concrete runtime capture command before process execution.
///
/// Contract constraints: this Module owns helper/grim argv shape, all-output vs
/// current-output selection, and focused output lookup; callers retain process
/// spawning and deterministic `ERR_*` mapping so taxonomy remains stable at the
/// backend seam.
pub fn resolve(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    options: Options,
) !Plan {
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
            const focused_output = try resolveFocusedOutput(allocator, io);
            errdefer allocator.free(focused_output);
            var plan = staticPlan(&.{ grim_path, "-o", focused_output, options.output_path });
            plan.owned_focused_output = focused_output;
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

fn resolveFocusedOutput(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    const niri_msg_result = process_exec.run(allocator, io, &.{ "niri", "msg", "--json", "focused-output" }, 65536, 0) catch {
        return error.BackendUnavailable;
    };
    defer niri_msg_result.deinit(allocator);
    if (!niri_msg_result.exitedZero()) return error.BackendUnavailable;

    const parsed = std.json.parseFromSlice(NiriFocusedOutput, allocator, niri_msg_result.stdout, .{
        .ignore_unknown_fields = true,
    }) catch return error.BackendUnavailable;
    defer parsed.deinit();
    if (parsed.value.name.len == 0) return error.BackendUnavailable;

    return allocator.dupe(u8, parsed.value.name);
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
