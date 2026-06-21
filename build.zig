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

    const preview_helper_bin = buildNativeGtkPreviewHelper(b, target, optimize);
    const install_preview_helper = b.addInstallFileWithDir(preview_helper_bin, .bin, "shaula-preview");
    b.getInstallStep().dependOn(&install_preview_helper.step);

    const settings_helper_bin = buildNativeGtkSettingsHelper(b, target, optimize);
    const install_settings_helper = b.addInstallFileWithDir(settings_helper_bin, .bin, "shaula-settings");
    b.getInstallStep().dependOn(&install_settings_helper.step);

    const crop_helper_bin = buildNativeCropImageHelper(b);
    const install_crop_helper = b.addInstallFileWithDir(crop_helper_bin, .bin, "shaula-crop-image");
    b.getInstallStep().dependOn(&install_crop_helper.step);

    const portal_helper_bin = buildNativePortalScreenshotHelper(b);
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

    const unit_tests = b.addTest(.{
        .root_module = unit_test_module,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);
    if (b.args) |args| run_unit_tests.addArgs(args);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);

    const preview_document_test = buildPreviewDocumentTest(b, target, optimize);
    test_step.dependOn(&preview_document_test.step);
}

fn buildPreviewDocumentTest(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Run {
    const preview_geometry_obj = buildZigObject(b, "preview-document-test-geometry", "src/preview/preview_geometry.zig", target, optimize);
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
    command.addFileArg(preview_geometry_obj);
    return command;
}

fn buildNativeGtkOverlayHelper(b: *std.Build) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
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

fn buildNativeGtkPreviewHelper(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) std.Build.LazyPath {
    const preview_geometry_obj = buildZigObject(b, "preview-geometry", "src/preview/preview_geometry.zig", target, optimize);
    const preview_image_io_obj = buildZigObject(b, "preview-image-io", "src/preview/preview_image_io.zig", target, optimize);
    const preview_clipboard_obj = buildZigObject(b, "preview-clipboard", "src/preview/preview_clipboard.zig", target, optimize);
    const preview_notify_obj = buildPreviewNotifyObject(b, target, optimize);

    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\shift
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4 gdk-pixbuf-2.0 cairo) -lm
        ,
        "build-shaula-preview",
    });
    const output = command.addOutputFileArg("shaula-preview");
    command.addFileArg(b.path("src/preview/native_gtk_preview.c"));
    command.addFileArg(b.path("src/preview/preview_actions.c"));
    command.addFileArg(b.path("src/preview/preview_annotations.c"));
    command.addFileArg(b.path("src/preview/preview_canvas.c"));
    command.addFileArg(b.path("src/preview/preview_commands.c"));
    command.addFileArg(b.path("src/preview/preview_document.c"));
    command.addFileArg(b.path("src/preview/preview_document_edit.c"));
    command.addFileArg(b.path("src/preview/preview_icons.c"));
    command.addFileArg(b.path("src/preview/preview_measure.c"));
    command.addFileArg(b.path("src/preview/preview_paths.c"));
    command.addFileArg(b.path("src/preview/preview_properties_hud.c"));
    command.addFileArg(b.path("src/preview/preview_properties_panel.c"));
    command.addFileArg(b.path("src/preview/preview_tool_defaults.c"));
    command.addFileArg(b.path("src/preview/preview_render.c"));
    command.addFileArg(b.path("src/preview/preview_spotlight.c"));
    command.addFileArg(b.path("src/preview/preview_state.c"));
    command.addFileArg(b.path("src/preview/preview_toolbar.c"));
    command.addFileArg(preview_geometry_obj);
    command.addFileArg(preview_image_io_obj);
    command.addFileArg(preview_clipboard_obj);
    command.addFileArg(preview_notify_obj);
    return output;
}

fn buildNativeGtkSettingsHelper(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) std.Build.LazyPath {
    const settings_config_obj = buildZigObject(b, "settings-config", "src/settings/settings_config.zig", target, optimize);

    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\out="$1"
        \\shift
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$@" \
        \\  -o "${out}" \
        \\ $(pkg-config --cflags --libs gtk4) -lm
        ,
        "build-shaula-settings",
    });
    const output = command.addOutputFileArg("shaula-settings");
    command.addFileArg(b.path("src/settings/native_gtk_settings.c"));
    command.addFileArg(settings_config_obj);
    return output;
}

fn buildNativeCropImageHelper(b: *std.Build) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$2" \
        \\  -o "$1" \
        \\ $(pkg-config --cflags --libs gdk-pixbuf-2.0) -lm
        ,
        "build-shaula-crop-image",
    });
    const output = command.addOutputFileArg("shaula-crop-image");
    command.addFileArg(b.path("src/capture/backends/native_crop_image.c"));
    return output;
}

fn buildNativePortalScreenshotHelper(b: *std.Build) std.Build.LazyPath {
    const command = b.addSystemCommand(&.{
        "sh",
        "-c",
        \\zig cc -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations \
        \\  "$2" \
        \\  -o "$1" \
        \\ $(pkg-config --cflags --libs gio-2.0 glib-2.0)
        ,
        "build-shaula-portal-screenshot",
    });
    const output = command.addOutputFileArg("shaula-portal-screenshot");
    command.addFileArg(b.path("src/capture/backends/native_portal_screenshot.c"));
    return output;
}

fn buildPreviewNotifyObject(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) std.Build.LazyPath {
    const module = b.createModule(.{
        .root_source_file = b.path("src/preview/preview_notify.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    addCCompatImport(b, module, target, optimize);
    const notify_request_module = b.createModule(.{
        .root_source_file = b.path("src/notify/request.zig"),
        .target = target,
        .optimize = optimize,
    });
    module.addImport("notify_request", notify_request_module);
    const object = b.addObject(.{
        .name = "preview-notify",
        .root_module = module,
    });
    return object.getEmittedBin();
}

fn buildZigObject(
    b: *std.Build,
    name: []const u8,
    source_path: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) std.Build.LazyPath {
    const module = b.createModule(.{
        .root_source_file = b.path(source_path),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    addCCompatImport(b, module, target, optimize);
    const object = b.addObject(.{
        .name = name,
        .root_module = module,
    });
    return object.getEmittedBin();
}

fn addCCompatImport(
    b: *std.Build,
    module: *std.Build.Module,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) void {
    const c_compat_module = b.createModule(.{
        .root_source_file = b.path("src/runtime/c_compat.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    module.addImport("c_compat", c_compat_module);
}
