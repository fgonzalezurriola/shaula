const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const main_module = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    const strip = b.option(bool, "strip", "Strip debug symbols from the binary") orelse false;
    const exe = b.addExecutable(.{
        .name = "shaula",
        .root_module = main_module,
    });
    exe.root_module.strip = strip;

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);

    const run_step = b.step("run", "Run shaula executable");
    run_step.dependOn(&run_cmd.step);

    const overlay_helper_bin = buildNativeGtkOverlayHelper(b);
    const install_overlay_helper = b.addInstallFileWithDir(overlay_helper_bin, .bin, "shaula-overlay");
    b.getInstallStep().dependOn(&install_overlay_helper.step);

    const preview_helper_bin = buildNativeGtkPreviewHelper(b);
    const install_preview_helper = b.addInstallFileWithDir(preview_helper_bin, .bin, "shaula-preview");
    b.getInstallStep().dependOn(&install_preview_helper.step);

    const install_preview_icons = b.addInstallDirectory(.{
        .source_dir = b.path("src/preview/icons/hicolor"),
        .install_dir = .{ .custom = "share" },
        .install_subdir = "icons/hicolor",
    });
    b.getInstallStep().dependOn(&install_preview_icons.step);

    const run_overlay_helper_cmd = b.addSystemCommand(&.{
        "sh",
        "-c",
        "exec ./zig-out/bin/shaula-overlay \"$@\"",
        "shaula-overlay",
    });
    run_overlay_helper_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_overlay_helper_cmd.addArgs(args);

    const run_overlay_helper_step = b.step("run-overlay", "Run overlay helper executable");
    run_overlay_helper_step.dependOn(&run_overlay_helper_cmd.step);

    const unit_test_module = b.createModule(.{
        .root_source_file = b.path("src/test_root.zig"),
        .target = target,
        .optimize = optimize,
    });

    const unit_tests = b.addTest(.{
        .root_module = unit_test_module,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    if (b.args) |args| run_unit_tests.addArgs(args);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}

fn buildNativeGtkOverlayHelper(b: *std.Build) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  -DSHAULA_OVERLAY_STANDALONE \
        \\  "$2" \
        \\  -o "$1" \
\\ $(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 gdk-pixbuf-2.0 cairo) -lm
,
        "build-shaula-overlay",
    });
    const output = command.addOutputFileArg("shaula-overlay");
    command.addFileArg(b.path("src/overlay/native_gtk_overlay.c"));
    return output;
}

fn buildNativeGtkPreviewHelper(b: *std.Build) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$2" \
        \\  -o "$1" \
\\ $(pkg-config --cflags --libs gtk4 gdk-pixbuf-2.0 cairo) -lm
,
        "build-shaula-preview",
    });
    const output = command.addOutputFileArg("shaula-preview");
    command.addFileArg(b.path("src/preview/native_gtk_preview.c"));
    return output;
}
