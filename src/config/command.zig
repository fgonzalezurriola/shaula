const std = @import("std");

const cli_json = @import("../cli/json.zig");
const protocol = @import("../ipc/protocol.zig");
const recovery_policy = @import("../recovery/policy.zig");
const config_types = @import("config.zig");
const loader = @import("loader.zig");
const niri_rule = @import("niri_rule.zig");
const niri_keybinds = @import("niri_keybinds.zig");
const manager = @import("manager.zig");

pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len < 3) {
        try writeErrorJson(io, "config", "ERR_CLI_USAGE", "usage: shaula config <show|init|save|niri-window-rule|niri-install|niri-keybinds|niri-keybinds-install|niri-keybinds-remove|niri-keybinds-status> --json", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const subcommand = argToSlice(argv[2]);
    const command = if (std.mem.eql(u8, subcommand, "show"))
        "config show"
    else if (std.mem.eql(u8, subcommand, "init"))
        "config init"
    else if (std.mem.eql(u8, subcommand, "save"))
        "config save"
    else if (std.mem.eql(u8, subcommand, "niri-window-rule"))
        "config niri-window-rule"
    else if (std.mem.eql(u8, subcommand, "niri-install"))
        "config niri-install"
    else if (std.mem.eql(u8, subcommand, "niri-keybinds"))
        "config niri-keybinds"
    else if (std.mem.eql(u8, subcommand, "niri-keybinds-install"))
        "config niri-keybinds-install"
    else if (std.mem.eql(u8, subcommand, "niri-keybinds-remove"))
        "config niri-keybinds-remove"
    else if (std.mem.eql(u8, subcommand, "niri-keybinds-status"))
        "config niri-keybinds-status"
    else {
        try writeErrorJson(io, "config", "ERR_CLI_USAGE", "unsupported config subcommand", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    };

    var json_mode = false;
    var dry_run = false;
    var force = false;
    var apply_niri = false;
    var path_override: ?[]const u8 = null;
    var save_config: config_types.Config = .{};
    var save_options_seen = false;
    var i: usize = 3;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--json")) {
            json_mode = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--dry-run")) {
            dry_run = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--force")) {
            force = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--apply-niri")) {
            if (!std.mem.eql(u8, subcommand, "save")) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--apply-niri is supported only for save", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            apply_niri = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--region-mode")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--region-mode is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            const value = argToSlice(argv[i]);
            save_config.capture.region_capture_mode = config_types.parseRegionCaptureMode(value) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --region-mode", false, null, null, "region_mode");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--preview-mode")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--preview-mode is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            const value = argToSlice(argv[i]);
            save_config.preview.window.mode = config_types.parsePreviewWindowMode(value) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --preview-mode", false, null, null, "preview_mode");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--focused")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--focused is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            save_config.preview.window.focused = parseBoolArg(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --focused", false, null, null, "focused");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--close-preview-on-save")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--close-preview-on-save is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            save_config.preview.window.close_preview_on_save = parseBoolArg(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --close-preview-on-save", false, null, null, "close_preview_on_save");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--width")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--width is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            save_config.preview.window.width = parsePositiveU32Arg(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --width", false, null, null, "width");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--height")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--height is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            save_config.preview.window.height = parsePositiveU32Arg(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --height", false, null, null, "height");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--floating-position")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--floating-position is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            const value = argToSlice(argv[i]);
            if (std.mem.eql(u8, value, "centered")) {
                save_config.preview.window.floating_position.x = null;
                save_config.preview.window.floating_position.y = null;
                save_config.preview.window.floating_position.relative_to = .top_left;
            } else if (std.mem.eql(u8, value, "top-left")) {
                save_config.preview.window.floating_position.x = 80;
                save_config.preview.window.floating_position.y = 80;
                save_config.preview.window.floating_position.relative_to = .top_left;
            } else if (std.mem.eql(u8, value, "top-right")) {
                save_config.preview.window.floating_position.x = 80;
                save_config.preview.window.floating_position.y = 80;
                save_config.preview.window.floating_position.relative_to = .top_right;
            } else {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --floating-position", false, null, null, "floating_position");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--after-quick-skip-preview") or
            std.mem.eql(u8, arg, "--after-area-skip-preview") or
            std.mem.eql(u8, arg, "--after-fullscreen-skip-preview") or
            std.mem.eql(u8, arg, "--after-all-screens-skip-preview") or
            std.mem.eql(u8, arg, "--after-quick-copy") or
            std.mem.eql(u8, arg, "--after-area-copy") or
            std.mem.eql(u8, arg, "--after-fullscreen-copy") or
            std.mem.eql(u8, arg, "--after-all-screens-copy") or
            std.mem.eql(u8, arg, "--after-quick-save") or
            std.mem.eql(u8, arg, "--after-area-save") or
            std.mem.eql(u8, arg, "--after-fullscreen-save") or
            std.mem.eql(u8, arg, "--after-all-screens-save") or
            std.mem.eql(u8, arg, "--notifications-success") or
            std.mem.eql(u8, arg, "--notifications-errors") or
            std.mem.eql(u8, arg, "--notifications-thumbnails"))
        {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "after-capture setting requires a boolean value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            const value = parseBoolArg(argToSlice(argv[i])) orelse {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid after-capture boolean", false, null, null, arg);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            applyBoolSetting(&save_config, arg, value);
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--save-folder")) {
            if (!std.mem.eql(u8, subcommand, "save") or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--save-folder is supported only for save and requires a value", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            const folder = argToSlice(argv[i]);
            if (!validSaveFolderArg(folder)) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --save-folder", false, null, null, "save_folder");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            save_config.capture.after.save_folder.set(argToSlice(argv[i])) catch {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "invalid --save-folder", false, null, null, "save_folder");
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            };
            save_options_seen = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--path")) {
            const path_supported = std.mem.eql(u8, subcommand, "niri-install") or
                std.mem.eql(u8, subcommand, "niri-keybinds-install") or
                std.mem.eql(u8, subcommand, "niri-keybinds-remove") or
                std.mem.eql(u8, subcommand, "niri-keybinds-status");
            if (!path_supported or i + 1 >= argv.len) {
                try writeErrorJson(io, command, "ERR_CLI_USAGE", "--path is supported only for niri-install, niri-keybinds-install, niri-keybinds-remove, and niri-keybinds-status", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
            }
            i += 1;
            path_override = argToSlice(argv[i]);
            continue;
        }
        try writeErrorJson(io, command, "ERR_CLI_USAGE", "unsupported flag", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (!json_mode) {
        try writeErrorJson(io, command, "ERR_CLI_USAGE", "--json is required", false, null, null, null);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    if (std.mem.eql(u8, subcommand, "init")) {
        var result = manager.initConfig(allocator, io, environ, .{
            .overwrite = force,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeInitJson(allocator, io, command, result);
        return 0;
    }

    if (std.mem.eql(u8, subcommand, "save")) {
        if (!save_options_seen) {
            try writeErrorJson(io, command, "ERR_CLI_USAGE", "config save requires at least one setting flag", false, null, null, null);
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        }
        if (!config_types.validateCaptureAfter(save_config.capture.after)) {
            try writeErrorJson(io, command, "ERR_CLI_USAGE", "skip preview requires copy or save", false, null, null, "capture.after");
            return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
        }
        var result = manager.saveConfig(allocator, io, environ, .{
            .config = save_config,
            .dry_run = dry_run,
            .force_canonical = force,
        }) catch |err| switch (err) {
            error.ConfigInvalid => {
                const path = try loader.resolveConfigPath(allocator, environ);
                defer if (path) |value| allocator.free(value);
                try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid configuration file", false, path, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
            },
            error.ConfigUnreadable => {
                const path = try loader.resolveConfigPath(allocator, environ);
                defer if (path) |value| allocator.free(value);
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration file is unreadable", false, path, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);

        var niri_result: ?manager.InstallResult = null;
        var niri_warnings: []const []const u8 = &.{};
        if (apply_niri) {
            var loaded_after_save = loader.load(allocator, io, environ) catch |err| switch (err) {
                error.ConfigInvalid => {
                    try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid configuration file after save", false, result.path, null, null);
                    return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
                },
                error.ConfigUnreadable => {
                    try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration file is unreadable after save", false, result.path, null, null);
                    return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
                },
                else => return err,
            };
            defer loaded_after_save.deinit(allocator);
            var rendered = try niri_rule.renderPreviewWindowRule(allocator, loaded_after_save.config);
            defer rendered.deinit(allocator);
            niri_warnings = rendered.warnings;
            niri_result = manager.installNiriRule(allocator, io, environ, rendered.kdl, .{
                .dry_run = dry_run,
            }) catch |err| switch (err) {
                error.ConfigInvalid => {
                    try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid Niri configuration managed block", false, null, null, null);
                    return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
                },
                error.ConfigUnreadable => {
                    try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                    return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
                },
                else => return err,
            };
        }
        defer if (niri_result) |*value| value.deinit(allocator);
        try writeSaveJson(allocator, io, command, result, niri_result, niri_warnings);
        return 0;
    }

    if (std.mem.eql(u8, subcommand, "niri-keybinds")) {
        const bin_path = try std.process.executablePathAlloc(io, allocator);
        defer allocator.free(bin_path);
        var rendered = try niri_keybinds.renderKeybinds(allocator, bin_path);
        defer rendered.deinit(allocator);
        try writeKeybindsJson(allocator, io, command, rendered);
        return 0;
    }

    if (std.mem.eql(u8, subcommand, "niri-keybinds-install")) {
        const bin_path = try std.process.executablePathAlloc(io, allocator);
        defer allocator.free(bin_path);
        var rendered = try niri_keybinds.renderKeybinds(allocator, bin_path);
        defer rendered.deinit(allocator);

        if (!force) {
            const conflicts = manager.detectKeybindConflicts(allocator, io, environ, path_override) catch |err| switch (err) {
                error.ConfigUnreadable => {
                    try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                    return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
                },
                else => return err,
            };
            defer {
                for (conflicts) |c| {
                    allocator.free(c.key);
                    allocator.free(c.action);
                    allocator.free(c.context);
                }
                allocator.free(conflicts);
            }
            if (conflicts.len > 0) {
                try writeKeybindsConflictJson(allocator, io, command, conflicts);
                return recovery_policy.exitCodeFor("ERR_NIRI_KEYBIND_CONFLICT");
            }
        }

        var result = manager.installNiriKeybinds(allocator, io, environ, rendered.kdl, .{
            .path_override = path_override,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigInvalid => {
                try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid Niri configuration managed block", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
            },
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeKeybindsInstallJson(allocator, io, command, result);
        return 0;
    }

    if (std.mem.eql(u8, subcommand, "niri-keybinds-remove")) {
        var result = manager.removeNiriKeybinds(allocator, io, environ, .{
            .path_override = path_override,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigInvalid => {
                try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid Niri configuration managed block", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
            },
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeKeybindsRemoveJson(allocator, io, command, result);
        return 0;
    }

    if (std.mem.eql(u8, subcommand, "niri-keybinds-status")) {
        const niri_path = manager.defaultNiriConfigPath(allocator, environ) catch null;
        defer if (niri_path) |p| allocator.free(p);

        const niri_detected = niri_path != null;
        const config_path = niri_path;

        var installed = false;
        if (config_path) |cp| {
            const content = if (manager.pathExists(io, cp))
                std.Io.Dir.cwd().readFileAlloc(io, cp, allocator, .limited(1024 * 1024)) catch null
            else
                null;
            if (content) |c| {
                defer allocator.free(c);
                installed = std.mem.indexOf(u8, c, niri_keybinds.managed_keybinds_begin) != null and
                    std.mem.indexOf(u8, c, niri_keybinds.managed_keybinds_end) != null;
            }
        }

        const conflicts = if (config_path != null)
            manager.detectKeybindConflicts(allocator, io, environ, path_override) catch &.{}
        else
            &[_]niri_keybinds.Conflict{};
        defer {
            for (conflicts) |c| {
                allocator.free(c.key);
                allocator.free(c.action);
                allocator.free(c.context);
            }
            allocator.free(conflicts);
        }

        try writeKeybindsStatusJson(allocator, io, command, niri_detected, config_path, installed, conflicts);
        return 0;
    }

    var loaded = loader.load(allocator, io, environ) catch |err| switch (err) {
        error.ConfigUnreadable => {
            const path = try loader.resolveConfigPath(allocator, environ);
            defer if (path) |value| allocator.free(value);
            try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "configuration file is unreadable", false, path, null, null);
            return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
        },
        error.ConfigInvalid => {
            const path = try loader.resolveConfigPath(allocator, environ);
            defer if (path) |value| allocator.free(value);
            try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid configuration file", false, path, null, null);
            return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
        },
        else => return err,
    };
    defer loaded.deinit(allocator);

    if (std.mem.eql(u8, subcommand, "show")) {
        try writeShowJson(allocator, io, command, loaded);
        return 0;
    }

    var rendered = try niri_rule.renderPreviewWindowRule(allocator, loaded.config);
    defer rendered.deinit(allocator);
    if (std.mem.eql(u8, subcommand, "niri-install")) {
        var result = manager.installNiriRule(allocator, io, environ, rendered.kdl, .{
            .path_override = path_override,
            .dry_run = dry_run,
        }) catch |err| switch (err) {
            error.ConfigInvalid => {
                try writeErrorJson(io, command, "ERR_CONFIG_INVALID", "invalid Niri configuration managed block", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_INVALID");
            },
            error.ConfigUnreadable => {
                try writeErrorJson(io, command, "ERR_CONFIG_UNREADABLE", "Niri configuration path could not be resolved", false, null, null, null);
                return recovery_policy.exitCodeFor("ERR_CONFIG_UNREADABLE");
            },
            else => return err,
        };
        defer result.deinit(allocator);
        try writeInstallJson(allocator, io, command, result, rendered.warnings);
        return 0;
    }
    try writeNiriRuleJson(allocator, io, command, loaded, rendered);
    return 0;
}

fn validSaveFolderArg(value: []const u8) bool {
    if (value.len == 0) return true;
    if (std.mem.indexOfAny(u8, value, "\"\\") != null) return false;
    if (std.mem.eql(u8, value, "~")) return true;
    if (std.mem.startsWith(u8, value, "~/")) return true;
    return std.fs.path.isAbsolute(value);
}

fn applyBoolSetting(config: *config_types.Config, arg: []const u8, value: bool) void {
    if (std.mem.eql(u8, arg, "--after-quick-skip-preview")) config.capture.after.quick.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-area-skip-preview")) config.capture.after.area.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-skip-preview")) config.capture.after.fullscreen.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-skip-preview")) config.capture.after.all_screens.skip_preview = value;
    if (std.mem.eql(u8, arg, "--after-quick-copy")) config.capture.after.quick.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-area-copy")) config.capture.after.area.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-copy")) config.capture.after.fullscreen.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-copy")) config.capture.after.all_screens.copy_to_clipboard = value;
    if (std.mem.eql(u8, arg, "--after-quick-save")) config.capture.after.quick.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-area-save")) config.capture.after.area.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-fullscreen-save")) config.capture.after.fullscreen.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--after-all-screens-save")) config.capture.after.all_screens.save_to_folder = value;
    if (std.mem.eql(u8, arg, "--notifications-success")) config.notifications.success = value;
    if (std.mem.eql(u8, arg, "--notifications-errors")) config.notifications.errors = value;
    if (std.mem.eql(u8, arg, "--notifications-thumbnails")) config.notifications.thumbnails = value;
}

fn writeInitJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, result: manager.InitResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"created\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            if (result.created) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
        },
    );
    try stdout.interface.flush();
}

fn writeInstallJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    result: manager.InstallResult,
    warnings: []const []const u8,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);
    const backup_path_json = try jsonNullableStringAlloc(allocator, result.backup_path);
    defer allocator.free(backup_path_json);
    const warnings_json = try warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"backup_path\":{s},\"installed\":{s},\"replaced\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            backup_path_json,
            if (result.installed) "true" else "false",
            if (result.replaced) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

fn writeSaveJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    result: manager.SaveResult,
    niri_result: ?manager.InstallResult,
    warnings: []const []const u8,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);
    const backup_path_json = try jsonNullableStringAlloc(allocator, result.backup_path);
    defer allocator.free(backup_path_json);
    const warnings_json = try warningsJson(allocator, warnings);
    defer allocator.free(warnings_json);

    const niri_json = if (niri_result) |niri| blk: {
        const niri_path_json = try jsonStringAlloc(allocator, niri.path);
        defer allocator.free(niri_path_json);
        const niri_backup_json = try jsonNullableStringAlloc(allocator, niri.backup_path);
        defer allocator.free(niri_backup_json);
        break :blk try std.fmt.allocPrint(
            allocator,
            "{{\"applied\":true,\"path\":{s},\"backup_path\":{s},\"changed\":{s},\"replaced\":{s},\"dry_run\":{s}}}",
            .{
                niri_path_json,
                niri_backup_json,
                if (niri.changed) "true" else "false",
                if (niri.replaced) "true" else "false",
                if (niri.dry_run) "true" else "false",
            },
        );
    } else try allocator.dupe(u8, "{\"applied\":false,\"changed\":false}");
    defer allocator.free(niri_json);

    var stdout_buffer: [16384]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"backup_path\":{s},\"created\":{s},\"changed\":{s},\"dry_run\":{s},\"niri\":{s}}},\"warnings\":{s}}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            backup_path_json,
            if (result.created) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
            niri_json,
            warnings_json,
        },
    );
    try stdout.interface.flush();
}

fn writeShowJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, loaded: loader.ConfigLoadResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonNullableStringAlloc(allocator, loaded.path);
    defer allocator.free(path_json);
    const config_json = try configJson(allocator, loaded.config);
    defer allocator.free(config_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"loaded\":{s},\"config\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts_json, path_json, if (loaded.loaded) "true" else "false", config_json },
    );
    try stdout.interface.flush();
}

fn writeNiriRuleJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, loaded: loader.ConfigLoadResult, rendered: niri_rule.RenderedRule) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);

    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonNullableStringAlloc(allocator, loaded.path);
    defer allocator.free(path_json);
    const app_id_json = try jsonStringAlloc(allocator, config_types.preview_app_id);
    defer allocator.free(app_id_json);
    const title_json = try jsonStringAlloc(allocator, config_types.preview_title);
    defer allocator.free(title_json);
    const kdl_json = try jsonStringAlloc(allocator, rendered.kdl);
    defer allocator.free(kdl_json);
    const warnings_json = try warningsJson(allocator, rendered.warnings);
    defer allocator.free(warnings_json);

    var stdout_buffer: [16384]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"loaded\":{s},\"target\":\"preview\",\"app_id\":{s},\"title\":{s},\"kdl\":{s}}},\"warnings\":{s}}}\n",
        .{ protocol.contract_version, command_json, ts_json, path_json, if (loaded.loaded) "true" else "false", app_id_json, title_json, kdl_json, warnings_json },
    );
    try stdout.interface.flush();
}

fn writeKeybindsJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, rendered: niri_keybinds.RenderedKeybinds) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const kdl_json = try jsonStringAlloc(allocator, rendered.kdl);
    defer allocator.free(kdl_json);

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"kdl\":{s}}},\"warnings\":[]}}\n",
        .{ protocol.contract_version, command_json, ts_json, kdl_json },
    );
    try stdout.interface.flush();
}

fn writeKeybindsInstallJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, result: manager.InstallResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);
    const backup_path_json = try jsonNullableStringAlloc(allocator, result.backup_path);
    defer allocator.free(backup_path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"backup_path\":{s},\"installed\":{s},\"replaced\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            backup_path_json,
            if (result.installed) "true" else "false",
            if (result.replaced) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
        },
    );
    try stdout.interface.flush();
}

fn writeKeybindsRemoveJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, result: manager.RemoveResult) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonStringAlloc(allocator, result.path);
    defer allocator.free(path_json);
    const backup_path_json = try jsonNullableStringAlloc(allocator, result.backup_path);
    defer allocator.free(backup_path_json);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"path\":{s},\"backup_path\":{s},\"removed\":{s},\"changed\":{s},\"dry_run\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            path_json,
            backup_path_json,
            if (result.removed) "true" else "false",
            if (result.changed) "true" else "false",
            if (result.dry_run) "true" else "false",
        },
    );
    try stdout.interface.flush();
}

fn writeKeybindsStatusJson(
    allocator: std.mem.Allocator,
    io: std.Io,
    command: []const u8,
    niri_detected: bool,
    config_path: ?[]const u8,
    installed: bool,
    conflicts: []const niri_keybinds.Conflict,
) !void {
    const ts = try nowIso8601(allocator, io);
    defer allocator.free(ts);
    const command_json = try jsonStringAlloc(allocator, command);
    defer allocator.free(command_json);
    const ts_json = try jsonStringAlloc(allocator, ts);
    defer allocator.free(ts_json);
    const path_json = try jsonNullableStringAlloc(allocator, config_path);
    defer allocator.free(path_json);

    var conflicts_json = std.ArrayList(u8).empty;
    defer conflicts_json.deinit(allocator);
    try conflicts_json.append(allocator, '[');
    for (conflicts, 0..) |conflict, idx| {
        if (idx > 0) try conflicts_json.append(allocator, ',');
        const key_json = try jsonStringAlloc(allocator, conflict.key);
        defer allocator.free(key_json);
        const action_json = try jsonStringAlloc(allocator, conflict.action);
        defer allocator.free(action_json);
        const context_json = try jsonStringAlloc(allocator, conflict.context);
        defer allocator.free(context_json);
        try conflicts_json.print(allocator, "{{\"key\":{s},\"action\":{s},\"context\":{s}}}", .{ key_json, action_json, context_json });
    }
    try conflicts_json.append(allocator, ']');

    var stdout_buffer: [8192]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &stdout_buffer);
    try stdout.interface.print(
        "{{\"ok\":true,\"contract_version\":\"{s}\",\"command\":{s},\"timestamp\":{s},\"result\":{{\"niri_detected\":{s},\"config_path\":{s},\"installed\":{s},\"conflicts\":{s}}},\"warnings\":[]}}\n",
        .{
            protocol.contract_version,
            command_json,
            ts_json,
            if (niri_detected) "true" else "false",
            path_json,
            if (installed) "true" else "false",
            conflicts_json.items,
        },
    );
    try stdout.interface.flush();
}

fn writeKeybindsConflictJson(allocator: std.mem.Allocator, io: std.Io, command: []const u8, conflicts: []const niri_keybinds.Conflict) !void {
    var conflicts_json = std.ArrayList(u8).empty;
    defer conflicts_json.deinit(allocator);
    try conflicts_json.append(allocator, '[');
    for (conflicts, 0..) |conflict, idx| {
        if (idx > 0) try conflicts_json.append(allocator, ',');
        const key_json = try jsonStringAlloc(allocator, conflict.key);
        defer allocator.free(key_json);
        const action_json = try jsonStringAlloc(allocator, conflict.action);
        defer allocator.free(action_json);
        const context_json = try jsonStringAlloc(allocator, conflict.context);
        defer allocator.free(context_json);
        try conflicts_json.print(allocator, "{{\"key\":{s},\"action\":{s},\"context\":{s}}}", .{ key_json, action_json, context_json });
    }
    try conflicts_json.append(allocator, ']');

    const details_json = try std.fmt.allocPrint(
        allocator,
        "{{\"conflicts\":{s}}}",
        .{conflicts_json.items},
    );
    defer allocator.free(details_json);
    try cli_json.writeErrorWithDetails(io, command, "ERR_NIRI_KEYBIND_CONFLICT", "existing keybind conflicts detected; use --force to overwrite", false, details_json);
}

fn configJson(allocator: std.mem.Allocator, config: config_types.Config) ![]u8 {
    const app_id_json = try jsonStringAlloc(allocator, config_types.preview_app_id);
    defer allocator.free(app_id_json);
    const title_json = try jsonStringAlloc(allocator, config_types.preview_title);
    defer allocator.free(title_json);
    const region_capture_mode_json = try jsonStringAlloc(allocator, config.capture.region_capture_mode.asString());
    defer allocator.free(region_capture_mode_json);
    const mode_json = try jsonStringAlloc(allocator, config.preview.window.mode.asString());
    defer allocator.free(mode_json);
    const display_json = try jsonStringAlloc(allocator, config.preview.window.default_column_display.asString());
    defer allocator.free(display_json);
    const relative_json = try jsonStringAlloc(allocator, config.preview.window.floating_position.relative_to.asString());
    defer allocator.free(relative_json);
    const width_json = try nullableU32Json(allocator, config.preview.window.width);
    defer allocator.free(width_json);
    const height_json = try nullableU32Json(allocator, config.preview.window.height);
    defer allocator.free(height_json);
    const x_json = try nullableI32Json(allocator, config.preview.window.floating_position.x);
    defer allocator.free(x_json);
    const y_json = try nullableI32Json(allocator, config.preview.window.floating_position.y);
    defer allocator.free(y_json);
    const save_folder_json = try jsonStringAlloc(allocator, config.capture.after.save_folder.value());
    defer allocator.free(save_folder_json);

    return std.fmt.allocPrint(
        allocator,
        "{{\"capture\":{{\"region_capture_mode\":{s},\"after\":{{\"save_folder\":{s},\"quick\":{s},\"area\":{s},\"fullscreen\":{s},\"all_screens\":{s}}}}},\"notifications\":{{\"success\":{s},\"errors\":{s},\"thumbnails\":{s}}},\"preview\":{{\"window\":{{\"app_id\":{s},\"title\":{s},\"mode\":{s},\"focused\":{s},\"close_preview_on_save\":{s},\"width\":{s},\"height\":{s},\"default_column_display\":{s},\"floating_position\":{{\"x\":{s},\"y\":{s},\"relative_to\":{s}}}}}}}}}",
        .{
            region_capture_mode_json,
            save_folder_json,
            afterModeJson(config.capture.after.quick),
            afterModeJson(config.capture.after.area),
            afterModeJson(config.capture.after.fullscreen),
            afterModeJson(config.capture.after.all_screens),
            if (config.notifications.success) "true" else "false",
            if (config.notifications.errors) "true" else "false",
            if (config.notifications.thumbnails) "true" else "false",
            app_id_json,
            title_json,
            mode_json,
            if (config.preview.window.focused) "true" else "false",
            if (config.preview.window.close_preview_on_save) "true" else "false",
            width_json,
            height_json,
            display_json,
            x_json,
            y_json,
            relative_json,
        },
    );
}

fn afterModeJson(mode: config_types.CaptureAfterModeConfig) []const u8 {
    if (mode.skip_preview and mode.copy_to_clipboard and mode.save_to_folder)
        return "{\"skip_preview\":true,\"copy_to_clipboard\":true,\"save_to_folder\":true}";
    if (mode.skip_preview and mode.copy_to_clipboard and !mode.save_to_folder)
        return "{\"skip_preview\":true,\"copy_to_clipboard\":true,\"save_to_folder\":false}";
    if (mode.skip_preview and !mode.copy_to_clipboard and mode.save_to_folder)
        return "{\"skip_preview\":true,\"copy_to_clipboard\":false,\"save_to_folder\":true}";
    if (!mode.skip_preview and mode.copy_to_clipboard and mode.save_to_folder)
        return "{\"skip_preview\":false,\"copy_to_clipboard\":true,\"save_to_folder\":true}";
    if (!mode.skip_preview and mode.copy_to_clipboard and !mode.save_to_folder)
        return "{\"skip_preview\":false,\"copy_to_clipboard\":true,\"save_to_folder\":false}";
    if (!mode.skip_preview and !mode.copy_to_clipboard and mode.save_to_folder)
        return "{\"skip_preview\":false,\"copy_to_clipboard\":false,\"save_to_folder\":true}";
    if (mode.skip_preview)
        return "{\"skip_preview\":true,\"copy_to_clipboard\":false,\"save_to_folder\":false}";
    return "{\"skip_preview\":false,\"copy_to_clipboard\":false,\"save_to_folder\":false}";
}

fn nullableU32Json(allocator: std.mem.Allocator, value: ?u32) ![]u8 {
    return cli_json.nullableU32Alloc(allocator, value);
}

fn nullableI32Json(allocator: std.mem.Allocator, value: ?i32) ![]u8 {
    return cli_json.nullableI32Alloc(allocator, value);
}

fn writeErrorJson(
    io: std.Io,
    command: []const u8,
    code: []const u8,
    message: []const u8,
    retryable: bool,
    path: ?[]const u8,
    line: ?u32,
    field: ?[]const u8,
) !void {
    var gpa: std.heap.DebugAllocator(.{}) = .init;
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const path_json = try jsonNullableStringAlloc(allocator, path);
    defer allocator.free(path_json);
    const field_json = try jsonNullableStringAlloc(allocator, field);
    defer allocator.free(field_json);
    const line_json = if (line) |value| try std.fmt.allocPrint(allocator, "{d}", .{value}) else try allocator.dupe(u8, "null");
    defer allocator.free(line_json);

    const details_json = try std.fmt.allocPrint(
        allocator,
        "{{\"path\":{s},\"line\":{s},\"field\":{s}}}",
        .{ path_json, line_json, field_json },
    );
    defer allocator.free(details_json);
    try cli_json.writeErrorWithDetails(io, command, code, message, retryable, details_json);
}

fn warningsJson(allocator: std.mem.Allocator, warnings: []const []const u8) ![]u8 {
    return cli_json.warningsAlloc(allocator, warnings);
}

fn jsonStringAlloc(allocator: std.mem.Allocator, value: []const u8) ![]u8 {
    return cli_json.stringAlloc(allocator, value);
}

fn jsonNullableStringAlloc(allocator: std.mem.Allocator, value: ?[]const u8) ![]u8 {
    return cli_json.nullableStringAlloc(allocator, value);
}

fn nowIso8601(allocator: std.mem.Allocator, io: std.Io) ![]u8 {
    return cli_json.nowIso8601(allocator, io);
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}

fn parseBoolArg(value: []const u8) ?bool {
    if (std.mem.eql(u8, value, "true")) return true;
    if (std.mem.eql(u8, value, "false")) return false;
    return null;
}

fn parsePositiveU32Arg(value: []const u8) ?u32 {
    if (std.mem.startsWith(u8, value, "-")) return null;
    const parsed = std.fmt.parseInt(u32, value, 10) catch return null;
    if (parsed == 0) return null;
    return parsed;
}
