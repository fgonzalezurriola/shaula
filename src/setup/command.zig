const std = @import("std");

const cli_json = @import("../cli/json.zig");
const recovery_policy = @import("../recovery/policy.zig");
const config_manager = @import("../config/manager.zig");
const config_loader = @import("../config/loader.zig");
const niri_rule = @import("../config/niri_rule.zig");
const niri_keybinds = @import("../config/niri_keybinds.zig");

const SetupOptions = struct {
    help: bool = false,
    assume_yes: bool = false,
    integrations: bool = true,
    niri: bool = true,
    noctalia: bool = true,
    force_niri_keybinds: bool = false,
    skip_niri_keybinds: bool = false,
    dry_run: bool = false,
};

const SetupState = struct {
    warnings: usize = 0,
};

const NoctaliaPaths = struct {
    config_dir: []u8,
    plugins_dir: []u8,
    plugin_dir: []u8,
    plugins_json: []u8,
    settings_json: []u8,

    fn deinit(self: *NoctaliaPaths, allocator: std.mem.Allocator) void {
        allocator.free(self.config_dir);
        allocator.free(self.plugins_dir);
        allocator.free(self.plugin_dir);
        allocator.free(self.plugins_json);
        allocator.free(self.settings_json);
    }
};

const NoctaliaResult = struct {
    plugin_files_installed: bool = false,
    plugins_json_changed: bool = false,
    settings_json_changed: bool = false,
};

/// Run the post-install user setup wizard.
///
/// Contract constraints: package managers must only install files; this command
/// owns user-scoped writes under XDG config paths and keeps integration prompts
/// on `/dev/tty` so `curl | sh` installers can reuse the same UX.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    const options = parseOptions(io, argv) catch return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    if (options.help) return 0;

    var state: SetupState = .{};
    try print(io,
        \\Shaula setup
        \\
    , .{});

    try ensureShaulaConfig(allocator, io, environ, options, &state);

    if (options.integrations) {
        try setupNiri(allocator, io, environ, options, &state);
        try setupNoctalia(allocator, io, environ, options, &state);
    }

    if (state.warnings == 0) {
        try print(io, "\nok: setup complete\n", .{});
    } else {
        try print(io, "\nsetup complete with {d} warning(s)\n", .{state.warnings});
    }
    return 0;
}

fn parseOptions(io: std.Io, argv: []const [*:0]const u8) !SetupOptions {
    var options: SetupOptions = .{};
    var i: usize = 2;
    while (i < argv.len) : (i += 1) {
        const arg = argToSlice(argv[i]);
        if (std.mem.eql(u8, arg, "--help")) {
            try printUsage(io);
            options.help = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--yes")) {
            options.assume_yes = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--no-integrations")) {
            options.integrations = false;
            continue;
        }
        if (std.mem.eql(u8, arg, "--no-niri")) {
            options.niri = false;
            continue;
        }
        if (std.mem.eql(u8, arg, "--no-noctalia")) {
            options.noctalia = false;
            continue;
        }
        if (std.mem.eql(u8, arg, "--niri-keybinds")) {
            options.force_niri_keybinds = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--skip-niri-keybinds")) {
            options.skip_niri_keybinds = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--force")) {
            options.force_niri_keybinds = true;
            continue;
        }
        if (std.mem.eql(u8, arg, "--dry-run")) {
            options.dry_run = true;
            continue;
        }
        try cli_json.writeBasicError(io, "setup", "ERR_CLI_USAGE", "unsupported setup flag", false);
        return error.InvalidArgument;
    }
    return options;
}

fn printUsage(io: std.Io) !void {
    try print(io,
        \\Usage: shaula setup [options]
        \\
        \\Configure Shaula for the current user after installation.
        \\
        \\Options:
        \\  --yes                  Accept detected integration prompts.
        \\  --no-integrations      Only create the Shaula config file.
        \\  --no-niri              Skip Niri integration.
        \\  --no-noctalia          Skip Noctalia integration.
        \\  --niri-keybinds        Install Niri keybinds without prompting.
        \\  --skip-niri-keybinds   Do not install Niri keybinds.
        \\  --force                Replace conflicting Niri keybinds block.
        \\  --dry-run              Print actions without writing files.
        \\
    , .{});
}

fn ensureShaulaConfig(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SetupOptions, state: *SetupState) !void {
    const result = config_manager.initConfig(allocator, io, environ, .{ .dry_run = options.dry_run }) catch |err| switch (err) {
        error.ConfigUnreadable => {
            try warn(io, state, "Shaula config path could not be resolved; set HOME or XDG_CONFIG_HOME.");
            return;
        },
        else => return err,
    };
    var owned = result;
    defer owned.deinit(allocator);

    if (owned.created) {
        try print(io, "ok: created config {s}\n", .{owned.path});
    } else {
        try print(io, "ok: kept existing config {s}\n", .{owned.path});
    }
}

fn setupNiri(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SetupOptions, state: *SetupState) !void {
    if (!options.niri) return;

    const niri_path = detectNiriConfig(allocator, io, environ) catch null;
    if (niri_path == null) {
        try print(io, "info: Niri config was not detected; skipped Niri integration.\n", .{});
        return;
    }
    defer allocator.free(niri_path.?);

    try print(io, "\nNiri integration:\n  detected {s}\n", .{niri_path.?});
    if (!options.assume_yes and !promptYesNo(io, "Install Shaula Niri preview rule? [y/N] ")) {
        try print(io, "info: skipped Niri preview rule.\n", .{});
    } else {
        try installNiriRule(allocator, io, environ, options, state);
    }

    if (options.skip_niri_keybinds) {
        try print(io, "info: skipped Niri keybinds.\n", .{});
        return;
    }
    const install_keys = options.force_niri_keybinds or options.assume_yes or promptYesNo(
        io,
        "Install Shaula Niri shortcuts Ctrl+Shift+1/2/3/4? [y/N] ",
    );
    if (!install_keys) {
        try print(io, "info: skipped Niri keybinds.\n", .{});
        return;
    }
    try installNiriKeybinds(allocator, io, environ, options, state);
}

fn installNiriRule(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SetupOptions, state: *SetupState) !void {
    var loaded = config_loader.load(allocator, io, environ) catch |err| switch (err) {
        error.ConfigInvalid => {
            try warn(io, state, "Shaula config is invalid; skipped Niri preview rule.");
            return;
        },
        error.ConfigUnreadable => {
            try warn(io, state, "Shaula config is unreadable; skipped Niri preview rule.");
            return;
        },
        else => return err,
    };
    defer loaded.deinit(allocator);

    var rendered = try niri_rule.renderPreviewWindowRule(allocator, loaded.config);
    defer rendered.deinit(allocator);

    var result = config_manager.installNiriRule(allocator, io, environ, rendered.kdl, .{ .dry_run = options.dry_run }) catch |err| switch (err) {
        error.ConfigInvalid => {
            try warn(io, state, "Niri config has malformed Shaula preview markers; skipped preview rule.");
            return;
        },
        error.ConfigUnreadable => {
            try warn(io, state, "Niri config path could not be resolved; skipped preview rule.");
            return;
        },
        else => return err,
    };
    defer result.deinit(allocator);

    if (result.changed) {
        try print(io, "ok: installed Niri preview rule in {s}\n", .{result.path});
        if (result.backup_path) |backup| try print(io, "  backup: {s}\n", .{backup});
    } else {
        try print(io, "ok: Niri preview rule already up to date in {s}\n", .{result.path});
    }
}

fn installNiriKeybinds(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SetupOptions, state: *SetupState) !void {
    const bin_path = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(bin_path);
    var rendered = try niri_keybinds.renderKeybinds(allocator, bin_path);
    defer rendered.deinit(allocator);

    if (!options.force_niri_keybinds) {
        const conflicts = config_manager.detectKeybindConflicts(allocator, io, environ, null) catch |err| switch (err) {
            error.ConfigUnreadable => {
                try warn(io, state, "Niri config path could not be resolved; skipped keybinds.");
                return;
            },
            else => return err,
        };
        defer freeConflicts(allocator, conflicts);
        if (conflicts.len > 0) {
            try warn(io, state, "existing Ctrl+Shift+1/2/3/4 Niri keybinds conflict with Shaula.");
            try print(io, "  run: shaula setup --niri-keybinds --force\n", .{});
            return;
        }
    }

    var result = config_manager.installNiriKeybinds(allocator, io, environ, rendered.kdl, .{ .dry_run = options.dry_run }) catch |err| switch (err) {
        error.ConfigInvalid => {
            try warn(io, state, "Niri config has malformed Shaula keybind markers; skipped keybinds.");
            return;
        },
        error.ConfigUnreadable => {
            try warn(io, state, "Niri config path could not be resolved; skipped keybinds.");
            return;
        },
        else => return err,
    };
    defer result.deinit(allocator);

    if (result.changed) {
        try print(io, "ok: installed Niri keybinds in {s}\n", .{result.path});
        if (result.backup_path) |backup| try print(io, "  backup: {s}\n", .{backup});
    } else {
        try print(io, "ok: Niri keybinds already up to date in {s}\n", .{result.path});
    }
}

fn setupNoctalia(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: SetupOptions, state: *SetupState) !void {
    if (!options.noctalia) return;

    var paths = try noctaliaPaths(allocator, environ);
    defer paths.deinit(allocator);

    if (!noctaliaDetected(io, paths)) {
        try print(io, "info: Noctalia config was not detected; skipped Noctalia integration.\n", .{});
        return;
    }

    try print(io, "\nNoctalia integration:\n  config {s}\n", .{paths.config_dir});
    const install_widget = options.assume_yes or promptYesNo(io, "Install Shaula Noctalia Bar Widget? [y/N] ");
    if (!install_widget) {
        try print(io, "info: skipped Noctalia Bar Widget.\n", .{});
        return;
    }

    const result = installNoctalia(allocator, io, environ, paths, options, state) catch |err| switch (err) {
        error.NoctaliaPluginSourceMissing => {
            try warn(io, state, "Noctalia plugin source files were not found; skipped widget install.");
            return;
        },
        error.NoctaliaConfigUnsupported => {
            try warn(io, state, "Noctalia JSON structure is unsupported; copied files only if possible.");
            return;
        },
        else => return err,
    };

    if (result.plugin_files_installed) try print(io, "ok: installed Noctalia plugin files in {s}\n", .{paths.plugin_dir});
    if (result.plugins_json_changed) try print(io, "ok: enabled Shaula in {s}\n", .{paths.plugins_json});
    if (result.settings_json_changed) try print(io, "ok: added plugin:shaula to {s}\n", .{paths.settings_json});
    if (!result.plugins_json_changed or !result.settings_json_changed) {
        try print(io, "info: if the widget is not visible, enable shaula and add plugin:shaula in Noctalia settings.\n", .{});
    }
}

const SetupError = error{
    NoctaliaPluginSourceMissing,
    NoctaliaConfigUnsupported,
};

/// Install the Noctalia widget into the current user's config directory.
///
/// Contract constraints: JSON files are backed up before mutation, plugin files
/// are marked as Shaula-managed, and unsupported JSON shapes return a warning
/// path instead of partially claiming success.
fn installNoctalia(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    paths: NoctaliaPaths,
    options: SetupOptions,
    state: *SetupState,
) !NoctaliaResult {
    const source_dir = try resolveNoctaliaPluginSource(allocator, io, environ);
    defer allocator.free(source_dir);

    var result: NoctaliaResult = .{};
    if (!options.dry_run) {
        try std.Io.Dir.cwd().createDirPath(io, paths.plugin_dir);
        inline for (.{ "manifest.json", "BarWidget.qml", "README.md" }) |file| {
            const source = try std.fmt.allocPrint(allocator, "{s}/{s}", .{ source_dir, file });
            defer allocator.free(source);
            const dest = try std.fmt.allocPrint(allocator, "{s}/{s}", .{ paths.plugin_dir, file });
            defer allocator.free(dest);
            try std.Io.Dir.copyFile(std.Io.Dir.cwd(), source, std.Io.Dir.cwd(), dest, io, .{ .make_path = true, .replace = true });
        }
        const marker_path = try std.fmt.allocPrint(allocator, "{s}/.shaula-managed", .{paths.plugin_dir});
        defer allocator.free(marker_path);
        try writeFileAtomic(io, marker_path, "installed-by=shaula\n");
    }
    result.plugin_files_installed = true;

    if (pathExists(io, paths.plugins_json)) {
        result.plugins_json_changed = enableNoctaliaPlugin(allocator, io, paths.plugins_json, options.dry_run) catch |err| switch (err) {
            error.NoctaliaConfigUnsupported => blk: {
                try warn(io, state, "failed to edit Noctalia plugins.json; enable shaula manually.");
                break :blk false;
            },
            else => return err,
        };
    } else {
        try warn(io, state, "Noctalia plugins.json is missing; copied plugin files only.");
    }

    if (pathExists(io, paths.settings_json)) {
        result.settings_json_changed = addNoctaliaWidget(allocator, io, paths.settings_json, options.dry_run) catch |err| switch (err) {
            error.NoctaliaConfigUnsupported => blk: {
                try warn(io, state, "failed to edit Noctalia settings.json; add plugin:shaula manually.");
                break :blk false;
            },
            else => return err,
        };
    } else {
        try warn(io, state, "Noctalia settings.json is missing; add plugin:shaula manually.");
    }

    return result;
}

fn enableNoctaliaPlugin(allocator: std.mem.Allocator, io: std.Io, path: []const u8, dry_run: bool) !bool {
    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024)) catch return error.NoctaliaConfigUnsupported;
    defer allocator.free(raw);
    var parsed = std.json.parseFromSlice(std.json.Value, allocator, raw, .{}) catch return error.NoctaliaConfigUnsupported;
    defer parsed.deinit();
    const arena = parsed.arena.allocator();

    if (parsed.value != .object) return error.NoctaliaConfigUnsupported;
    var root = &parsed.value.object;
    const version = root.get("version") orelse return error.NoctaliaConfigUnsupported;
    if (version != .integer or version.integer != 2) return error.NoctaliaConfigUnsupported;

    const states_value = root.getPtr("states") orelse blk: {
        try root.put(arena, "states", .{ .object = .empty });
        break :blk root.getPtr("states").?;
    };
    if (states_value.* != .object) return error.NoctaliaConfigUnsupported;
    var states = &states_value.object;

    const shaula_value = states.getPtr("shaula") orelse blk: {
        try states.put(arena, "shaula", .{ .object = .empty });
        break :blk states.getPtr("shaula").?;
    };
    if (shaula_value.* != .object) {
        shaula_value.* = .{ .object = .empty };
    }
    try shaula_value.object.put(arena, "enabled", .{ .bool = true });
    if (shaula_value.object.get("sourceUrl") == null) {
        try shaula_value.object.put(arena, "sourceUrl", .{ .string = "local" });
    }

    const next = try jsonAlloc(allocator, parsed.value, .indent_2);
    defer allocator.free(next);
    if (std.mem.eql(u8, raw, next)) return false;
    if (!dry_run) {
        const backup_path = try backupFile(allocator, io, path, raw);
        allocator.free(backup_path);
        try writeFileAtomic(io, path, next);
    }
    return true;
}

fn addNoctaliaWidget(allocator: std.mem.Allocator, io: std.Io, path: []const u8, dry_run: bool) !bool {
    const raw = std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024)) catch return error.NoctaliaConfigUnsupported;
    defer allocator.free(raw);
    var parsed = std.json.parseFromSlice(std.json.Value, allocator, raw, .{}) catch return error.NoctaliaConfigUnsupported;
    defer parsed.deinit();
    const arena = parsed.arena.allocator();

    if (parsed.value != .object) return error.NoctaliaConfigUnsupported;
    var root = &parsed.value.object;
    const bar_value = root.getPtr("bar") orelse return error.NoctaliaConfigUnsupported;
    if (bar_value.* != .object) return error.NoctaliaConfigUnsupported;
    var bar = &bar_value.object;
    const widgets_value = bar.getPtr("widgets") orelse return error.NoctaliaConfigUnsupported;
    if (widgets_value.* != .object) return error.NoctaliaConfigUnsupported;
    var widgets = &widgets_value.object;

    inline for (.{ "left", "center", "right" }) |section| {
        const value = widgets.getPtr(section) orelse blk: {
            try widgets.put(arena, section, .{ .array = std.json.Array.init(arena) });
            break :blk widgets.getPtr(section).?;
        };
        if (value.* != .array) {
            value.* = .{ .array = std.json.Array.init(arena) };
        }
    }

    inline for (.{ "left", "center", "right" }) |section| {
        const value = widgets.getPtr(section).?;
        for (value.array.items) |item| {
            if (item == .object) {
                const id = item.object.get("id") orelse continue;
                if (id == .string and std.mem.eql(u8, id.string, "plugin:shaula")) return false;
            }
        }
    }

    var widget: std.json.Value = .{ .object = .empty };
    try widget.object.put(arena, "id", .{ .string = "plugin:shaula" });
    try widgets.getPtr("right").?.array.append(widget);

    const next = try jsonAlloc(allocator, parsed.value, .indent_4);
    defer allocator.free(next);
    if (!dry_run) {
        const backup_path = try backupFile(allocator, io, path, raw);
        allocator.free(backup_path);
        try writeFileAtomic(io, path, next);
    }
    return true;
}

fn detectNiriConfig(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) !?[]u8 {
    if (envSlice(environ, "NIRI_CONFIG")) |value| {
        if (value.len > 0 and pathExists(io, value)) return try allocator.dupe(u8, value);
    }
    const xdg = try resolveXdgConfigHome(allocator, environ);
    defer if (xdg) |path| allocator.free(path);
    if (xdg) |dir| {
        const config = try std.fmt.allocPrint(allocator, "{s}/niri/config.kdl", .{dir});
        if (pathExists(io, config)) return config;
        allocator.free(config);
        const cfg = try std.fmt.allocPrint(allocator, "{s}/niri/cfg", .{dir});
        if (pathExists(io, cfg)) return cfg;
        allocator.free(cfg);
    }
    if (pathExists(io, "/etc/niri/config.kdl")) return try allocator.dupe(u8, "/etc/niri/config.kdl");
    return null;
}

fn noctaliaPaths(allocator: std.mem.Allocator, environ: std.process.Environ) !NoctaliaPaths {
    const xdg = try resolveXdgConfigHome(allocator, environ) orelse return error.ConfigUnreadable;
    defer allocator.free(xdg);
    const config_dir = try std.fmt.allocPrint(allocator, "{s}/noctalia", .{xdg});
    errdefer allocator.free(config_dir);
    const plugins_dir = try std.fmt.allocPrint(allocator, "{s}/plugins", .{config_dir});
    errdefer allocator.free(plugins_dir);
    const plugin_dir = try std.fmt.allocPrint(allocator, "{s}/shaula", .{plugins_dir});
    errdefer allocator.free(plugin_dir);
    const plugins_json = try std.fmt.allocPrint(allocator, "{s}/plugins.json", .{config_dir});
    errdefer allocator.free(plugins_json);
    const settings_json = try std.fmt.allocPrint(allocator, "{s}/settings.json", .{config_dir});
    errdefer allocator.free(settings_json);
    return .{
        .config_dir = config_dir,
        .plugins_dir = plugins_dir,
        .plugin_dir = plugin_dir,
        .plugins_json = plugins_json,
        .settings_json = settings_json,
    };
}

fn noctaliaDetected(io: std.Io, paths: NoctaliaPaths) bool {
    return pathExists(io, paths.config_dir) or
        pathExists(io, paths.plugins_dir) or
        pathExists(io, paths.plugins_json) or
        pathExists(io, paths.settings_json);
}

fn resolveNoctaliaPluginSource(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (envSlice(environ, "SHAULA_NOCTALIA_PLUGIN_SOURCE")) |override| {
        if (try validPluginSource(allocator, io, override)) return allocator.dupe(u8, override);
    }

    const exe = std.process.executablePathAlloc(io, allocator) catch null;
    defer if (exe) |path| allocator.free(path);
    if (exe) |path| {
        if (std.fs.path.dirname(path)) |bin_dir| {
            if (std.fs.path.dirname(bin_dir)) |prefix| {
                const installed = try std.fmt.allocPrint(allocator, "{s}/share/shaula/integrations/noctalia/shaula", .{prefix});
                if (try validPluginSource(allocator, io, installed)) return installed;
                allocator.free(installed);
            }
        }
    }

    inline for (.{ "/usr/share/shaula/integrations/noctalia/shaula", "/usr/local/share/shaula/integrations/noctalia/shaula", "integrations/noctalia/shaula" }) |candidate| {
        if (try validPluginSource(allocator, io, candidate)) return allocator.dupe(u8, candidate);
    }
    return error.NoctaliaPluginSourceMissing;
}

fn validPluginSource(allocator: std.mem.Allocator, io: std.Io, dir: []const u8) !bool {
    const manifest = try std.fmt.allocPrint(allocator, "{s}/manifest.json", .{dir});
    defer allocator.free(manifest);
    const widget = try std.fmt.allocPrint(allocator, "{s}/BarWidget.qml", .{dir});
    defer allocator.free(widget);
    return pathExists(io, manifest) and pathExists(io, widget);
}

fn resolveXdgConfigHome(allocator: std.mem.Allocator, environ: std.process.Environ) !?[]u8 {
    if (envSlice(environ, "XDG_CONFIG_HOME")) |value| {
        if (value.len > 0) return try allocator.dupe(u8, value);
    }
    if (envSlice(environ, "HOME")) |home| {
        if (home.len > 0) return try std.fmt.allocPrint(allocator, "{s}/.config", .{home});
    }
    return null;
}

fn promptYesNo(io: std.Io, question: []const u8) bool {
    var tty = std.Io.Dir.openFileAbsolute(io, "/dev/tty", .{ .mode = .read_write }) catch return false;
    defer tty.close(io);

    var write_buffer: [512]u8 = undefined;
    var tty_writer = tty.writer(io, &write_buffer);
    tty_writer.interface.writeAll(question) catch return false;
    tty_writer.interface.flush() catch return false;

    var read_buffer: [512]u8 = undefined;
    var tty_reader = tty.reader(io, &read_buffer);
    const line = (tty_reader.interface.takeDelimiter('\n') catch return false) orelse return false;
    const answer = std.mem.trim(u8, line, " \t\r\n");
    return std.ascii.eqlIgnoreCase(answer, "y") or std.ascii.eqlIgnoreCase(answer, "yes");
}

fn backupFile(allocator: std.mem.Allocator, io: std.Io, path: []const u8, contents: []const u8) ![]u8 {
    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    const backup_path = try std.fmt.allocPrint(allocator, "{s}.shaula-backup-{d}", .{ path, millis });
    errdefer allocator.free(backup_path);
    try writeFileAtomic(io, backup_path, contents);
    return backup_path;
}

fn writeFileAtomic(io: std.Io, path: []const u8, contents: []const u8) !void {
    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }
    const tmp_path = try std.fmt.allocPrint(std.heap.page_allocator, "{s}.tmp", .{path});
    defer std.heap.page_allocator.free(tmp_path);
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = tmp_path, .data = contents, .flags = .{ .truncate = true } });
    if (std.fs.path.isAbsolute(path)) {
        try std.Io.Dir.renameAbsolute(tmp_path, path, io);
    } else {
        try std.Io.Dir.cwd().rename(tmp_path, std.Io.Dir.cwd(), path, io);
    }
}

fn jsonAlloc(allocator: std.mem.Allocator, value: std.json.Value, comptime whitespace: @TypeOf((std.json.Stringify.Options{}).whitespace)) ![]u8 {
    const encoded = try std.json.Stringify.valueAlloc(allocator, value, .{ .whitespace = whitespace });
    defer allocator.free(encoded);
    return std.fmt.allocPrint(allocator, "{s}\n", .{encoded});
}

fn freeConflicts(allocator: std.mem.Allocator, conflicts: []const niri_keybinds.Conflict) void {
    for (conflicts) |conflict| {
        allocator.free(conflict.key);
        allocator.free(conflict.action);
        allocator.free(conflict.context);
    }
    allocator.free(conflicts);
}

fn warn(io: std.Io, state: *SetupState, comptime message: []const u8) !void {
    state.warnings += 1;
    try print(io, "warning: " ++ message ++ "\n", .{});
}

fn pathExists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
}

fn envSlice(environ: std.process.Environ, key: []const u8) ?[]const u8 {
    if (environ.getPosix(key)) |value| return std.mem.sliceTo(value, 0);
    return null;
}

fn print(io: std.Io, comptime fmt: []const u8, args: anytype) !void {
    var buffer: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writer(io, &buffer);
    try stdout.interface.print(fmt, args);
    try stdout.interface.flush();
}

fn argToSlice(arg: [*:0]const u8) []const u8 {
    return std.mem.sliceTo(arg, 0);
}
