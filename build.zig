const std = @import("std");

const OverlayUiDeps = struct {
    raylib: *std.Build.Module,
    clay: *std.Build.Module,
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const overlay_ui_deps = resolveOverlayUiDeps(b, target, optimize);

    const main_module = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    attachOverlayUiImports(main_module, overlay_ui_deps);

    const exe = b.addExecutable(.{
        .name = "shaula",
        .root_module = main_module,
    });

    b.installArtifact(exe);

    const overlay_spike_module = b.createModule(.{
        .root_source_file = b.path("src/overlay/spike_main.zig"),
        .target = target,
        .optimize = optimize,
    });
    attachOverlayUiImports(overlay_spike_module, overlay_ui_deps);

    const overlay_spike_exe = b.addExecutable(.{
        .name = "shaula-overlay-feasibility-spike",
        .root_module = overlay_spike_module,
    });

    b.installArtifact(overlay_spike_exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);

    const run_step = b.step("run", "Run shaula executable");
    run_step.dependOn(&run_cmd.step);

    const run_overlay_spike_cmd = b.addRunArtifact(overlay_spike_exe);
    run_overlay_spike_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_overlay_spike_cmd.addArgs(args);

    const run_overlay_spike_step = b.step("run-overlay-spike", "Run overlay feasibility spike executable");
    run_overlay_spike_step.dependOn(&run_overlay_spike_cmd.step);

    const overlay_helper_module = b.createModule(.{
        .root_source_file = b.path("src/overlay/helper_main.zig"),
        .target = target,
        .optimize = optimize,
    });
    attachOverlayUiImports(overlay_helper_module, overlay_ui_deps);

    const overlay_helper_exe = b.addExecutable(.{
        .name = "shaula-overlay",
        .root_module = overlay_helper_module,
    });

    b.installArtifact(overlay_helper_exe);

    const run_overlay_helper_cmd = b.addRunArtifact(overlay_helper_exe);
    run_overlay_helper_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_overlay_helper_cmd.addArgs(args);

    const run_overlay_helper_step = b.step("run-overlay", "Run overlay helper executable");
    run_overlay_helper_step.dependOn(&run_overlay_helper_cmd.step);

    const unit_test_module = b.createModule(.{
        .root_source_file = b.path("src/test_root.zig"),
        .target = target,
        .optimize = optimize,
    });
    attachOverlayUiImports(unit_test_module, overlay_ui_deps);

    const unit_tests = b.addTest(.{
        .root_module = unit_test_module,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    if (b.args) |args| run_unit_tests.addArgs(args);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}

/// Resolves Raylib/Clay imports for build graph consumers.
///
/// Contract constraints:
/// - default behavior is deterministic stub fallback when dependencies are absent,
///   keeping non-UI builds and tests stable.
/// - `-Dshaula_require_ui_deps=true` enforces real dependency presence with
///   deterministic `ERR_*`-token panic messages.
/// - `-Dshaula_force_missing_raylib=true` provides a deterministic QA lane for
///   missing-dependency failure evidence.
fn resolveOverlayUiDeps(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) OverlayUiDeps {
    const require_ui_deps = b.option(bool, "shaula_require_ui_deps", "Require real Raylib/Clay dependencies for overlay builds") orelse false;
    const use_ui_deps = b.option(bool, "shaula_use_ui_deps", "Use build.zig.zon Raylib/Clay dependencies when declared") orelse false;
    const force_missing_raylib = b.option(bool, "shaula_force_missing_raylib", "Deterministically simulate missing raylib dependency for QA") orelse false;

    if (force_missing_raylib) {
        @panic("ERR_DEP_RAYLIB_MISSING_SIMULATED: disable -Dshaula_force_missing_raylib or provide raylib dependency wiring");
    }

    const write_files = b.addWriteFiles();
    const raylib_stub_module = b.createModule(.{
        .root_source_file = write_files.add("generated/stubs/raylib.zig", "pub const shaula_stub = true;\n"),
        .target = target,
        .optimize = optimize,
    });
    const clay_stub_module = b.createModule(.{
        .root_source_file = write_files.add("generated/stubs/clay.zig", "pub const shaula_stub = true;\n"),
        .target = target,
        .optimize = optimize,
    });

    const raylib_module = blk: {
        if (!use_ui_deps) {
            if (require_ui_deps) @panic("ERR_DEP_RAYLIB_MISSING: enable -Dshaula_use_ui_deps and add raylib dependency wiring");
            break :blk raylib_stub_module;
        }
        const raylib_dep = b.lazyDependency("raylib", .{ .target = target, .optimize = optimize }) orelse {
            if (require_ui_deps) @panic("ERR_DEP_RAYLIB_MISSING: add raylib dependency in build.zig.zon");
            break :blk raylib_stub_module;
        };
        break :blk raylib_dep.module("raylib");
    };

    const clay_module = blk: {
        if (!use_ui_deps) {
            if (require_ui_deps) @panic("ERR_DEP_CLAY_MISSING: enable -Dshaula_use_ui_deps and add clay dependency wiring");
            break :blk clay_stub_module;
        }
        const clay_dep = b.lazyDependency("clay", .{ .target = target, .optimize = optimize }) orelse {
            if (require_ui_deps) @panic("ERR_DEP_CLAY_MISSING: add clay dependency in build.zig.zon");
            break :blk clay_stub_module;
        };
        break :blk clay_dep.module("clay");
    };

    return .{
        .raylib = raylib_module,
        .clay = clay_module,
    };
}

fn attachOverlayUiImports(module: *std.Build.Module, deps: OverlayUiDeps) void {
    module.addImport("raylib", deps.raylib);
    module.addImport("clay", deps.clay);
}
