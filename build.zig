const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const main_module = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    addRuntimeC(b, main_module);

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

    const overlay_helper_bin = buildNativeGtkOverlayHelper(b, strip);
    const install_overlay_helper = b.addInstallFileWithDir(overlay_helper_bin, .bin, "shaula-overlay");
    b.getInstallStep().dependOn(&install_overlay_helper.step);

    const preview_helper_bin = buildNativeGtkPreviewHelper(b, strip);
    const install_preview_helper = b.addInstallFileWithDir(preview_helper_bin, .bin, "shaula-preview");
    b.getInstallStep().dependOn(&install_preview_helper.step);

    const settings_helper_bin = buildNativeGtkSettingsHelper(b, strip);
    const install_settings_helper = b.addInstallFileWithDir(settings_helper_bin, .bin, "shaula-settings");
    b.getInstallStep().dependOn(&install_settings_helper.step);

    const crop_helper_bin = buildNativeCropImageHelper(b, strip);
    const install_crop_helper = b.addInstallFileWithDir(crop_helper_bin, .bin, "shaula-crop-image");
    b.getInstallStep().dependOn(&install_crop_helper.step);

    const portal_helper_bin = buildNativePortalScreenshotHelper(b, strip);
    const install_portal_helper = b.addInstallFileWithDir(portal_helper_bin, .bin, "shaula-portal-screenshot");
    b.getInstallStep().dependOn(&install_portal_helper.step);

    const install_preview_icons = b.addInstallDirectory(.{
        .source_dir = b.path("src/preview/icons/hicolor"),
        .install_dir = .{ .custom = "share" },
        .install_subdir = "icons/hicolor",
    });
    b.getInstallStep().dependOn(&install_preview_icons.step);

    const install_noctalia_integration = b.addInstallDirectory(.{
        .source_dir = b.path("integrations/noctalia/shaula"),
        .install_dir = .{ .custom = "share" },
        .install_subdir = "shaula/integrations/noctalia/shaula",
    });
    b.getInstallStep().dependOn(&install_noctalia_integration.step);

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
    addRuntimeC(b, unit_test_module);

    const unit_tests = b.addTest(.{
        .root_module = unit_test_module,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    if (b.args) |args| run_unit_tests.addArgs(args);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);

    const preview_document_test = buildPreviewDocumentTest(b);
    test_step.dependOn(&preview_document_test.step);
}

fn addRuntimeC(b: *std.Build, module: *std.Build.Module) void {
    module.addIncludePath(b.path("src"));
    module.link_libc = true;
    module.linkSystemLibrary("glib-2.0", .{ .use_pkg_config = .force });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/env.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/paths.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/tool_lookup.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/helper_resolution.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/previous_area_store.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/capture_session_lock.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/runtime/process_exec.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/core/capture_mode.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/errors/taxonomy.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
    module.addCSourceFile(.{
        .file = b.path("src/preview/preview_result.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Wpedantic" },
    });
}

fn buildPreviewDocumentTest(b: *std.Build) *std.Build.Step.Run {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\shift
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4 gdk-pixbuf-2.0 cairo pangocairo) -lm
        \\"${out}"
        ,
        "preview-document-test",
    });
    _ = command.addOutputFileArg("preview-document-test");
    command.addFileArg(b.path("src/preview/preview_document_test.c"));
    command.addFileArg(b.path("src/preview/preview_document.c"));
    command.addFileArg(b.path("src/preview/preview_annotations.c"));
    command.addFileArg(b.path("src/preview/preview_paste_placement.c"));
    command.addFileArg(b.path("src/preview/preview_geometry.c"));
    return command;
}

fn buildNativeGtkOverlayHelper(b: *std.Build, strip: bool) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\strip_flag="$2"
        \\shift 2
        \\if [ -n "${strip_flag}" ]; then
        \\  set -- "${strip_flag}" "$@"
        \\fi
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  -DSHAULA_OVERLAY_STANDALONE \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0)
        ,
        "build-shaula-overlay",
    });
    const output = command.addOutputFileArg("shaula-overlay");
    command.addArg(if (strip) "-s" else "");
    command.addFileArg(b.path("src/overlay/native_gtk_overlay.c"));
    return output;
}

fn buildNativeGtkPreviewHelper(b: *std.Build, strip: bool) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\strip_flag="$2"
        \\shift 2
        \\if [ -n "${strip_flag}" ]; then
        \\  set -- "${strip_flag}" "$@"
        \\fi
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4) -lm
        ,
        "build-shaula-preview",
    });
    const output = command.addOutputFileArg("shaula-preview");
    command.addArg(if (strip) "-s" else "");
    command.addFileArg(b.path("src/preview/native_gtk_preview.c"));
    command.addFileArg(b.path("src/preview/preview_actions.c"));
    command.addFileArg(b.path("src/preview/preview_clipboard.c"));
    command.addFileArg(b.path("src/preview/preview_image_io.c"));
    command.addFileArg(b.path("src/preview/preview_notify.c"));
    command.addFileArg(b.path("src/preview/preview_action_callbacks.c"));
    command.addFileArg(b.path("src/preview/preview_annotation_editor.c"));
    command.addFileArg(b.path("src/preview/preview_annotations.c"));
    command.addFileArg(b.path("src/preview/preview_canvas.c"));
    command.addFileArg(b.path("src/preview/preview_commands.c"));
    command.addFileArg(b.path("src/preview/preview_document.c"));
    command.addFileArg(b.path("src/preview/preview_document_edit.c"));
    command.addFileArg(b.path("src/preview/preview_gesture.c"));
    command.addFileArg(b.path("src/preview/preview_geometry.c"));
    command.addFileArg(b.path("src/preview/preview_icons.c"));
    command.addFileArg(b.path("src/preview/preview_measure.c"));
    command.addFileArg(b.path("src/preview/preview_paths.c"));
    command.addFileArg(b.path("src/preview/preview_paste_placement.c"));
    command.addFileArg(b.path("src/preview/preview_properties_hud.c"));
    command.addFileArg(b.path("src/preview/preview_properties_panel.c"));
    command.addFileArg(b.path("src/preview/preview_tool_defaults.c"));
    command.addFileArg(b.path("src/preview/preview_render.c"));
    command.addFileArg(b.path("src/preview/preview_spotlight.c"));
    command.addFileArg(b.path("src/preview/preview_state.c"));
    command.addFileArg(b.path("src/preview/preview_system_clipboard.c"));
    command.addFileArg(b.path("src/preview/preview_toolbar.c"));
    return output;
}

fn buildNativeGtkSettingsHelper(b: *std.Build, strip: bool) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\strip_flag="$2"
        \\shift 2
        \\if [ -n "${strip_flag}" ]; then
        \\  set -- "${strip_flag}" "$@"
        \\fi
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4)
        ,
        "build-shaula-settings",
    });
    const output = command.addOutputFileArg("shaula-settings");
    command.addArg(if (strip) "-s" else "");
    command.addFileArg(b.path("src/settings/native_gtk_settings.c"));
    command.addFileArg(b.path("src/settings/settings_config.c"));
    command.addFileArg(b.path("src/settings/settings_process.c"));
    return output;
}

fn buildNativeCropImageHelper(b: *std.Build, strip: bool) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\strip_flag="$2"
        \\shift 2
        \\if [ -n "${strip_flag}" ]; then
        \\  set -- "${strip_flag}" "$@"
        \\fi
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gdk-pixbuf-2.0)
        ,
        "build-shaula-crop-image",
    });
    const output = command.addOutputFileArg("shaula-crop-image");
    command.addArg(if (strip) "-s" else "");
    command.addFileArg(b.path("src/capture/backends/native_crop_image.c"));
    return output;
}

fn buildNativePortalScreenshotHelper(b: *std.Build, strip: bool) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\strip_flag="$2"
        \\shift 2
        \\if [ -n "${strip_flag}" ]; then
        \\  set -- "${strip_flag}" "$@"
        \\fi
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gio-2.0)
        ,
        "build-shaula-portal-screenshot",
    });
    const output = command.addOutputFileArg("shaula-portal-screenshot");
    command.addArg(if (strip) "-s" else "");
    command.addFileArg(b.path("src/capture/backends/native_portal_screenshot.c"));
    return output;
}
