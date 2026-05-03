const std = @import("std");

const loader = @import("loader.zig");

pub const default_config_toml =
    \\[capture]
    \\# live keeps the desktop updating while selecting. frozen shows a still
    \\# screen while selecting for transient states.
    \\region_capture_mode = "live"
    \\
    \\[preview.window]
    \\mode = "floating"
    \\focused = true
    \\width = 1100
    \\height = 720
    \\default_column_display = "normal"
    \\
    \\[preview.window.floating_position]
    \\# x and y are optional. When both are set, Shaula's generated Niri rule
    \\# includes default-floating-position.
    \\# x = 80
    \\# y = 80
    \\relative_to = "top-left"
    \\
;

pub const managed_block_begin = "// BEGIN SHAULA PREVIEW WINDOW RULE";
pub const managed_block_end = "// END SHAULA PREVIEW WINDOW RULE";
const max_backup_attempts = 100;

pub const InitOptions = struct {
    overwrite: bool = false,
    dry_run: bool = false,
};

pub const InitResult = struct {
    path: []u8,
    created: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *InitResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
    }
};

pub const InstallOptions = struct {
    path_override: ?[]const u8 = null,
    dry_run: bool = false,
};

pub const InstallResult = struct {
    path: []u8,
    backup_path: ?[]u8,
    installed: bool,
    replaced: bool,
    changed: bool,
    dry_run: bool,

    pub fn deinit(self: *InstallResult, allocator: std.mem.Allocator) void {
        allocator.free(self.path);
        if (self.backup_path) |path| allocator.free(path);
    }
};

pub fn defaultConfigPath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    const resolved = try loader.resolveConfigPath(allocator, environ);
    if (resolved) |path| return path;
    return error.ConfigUnreadable;
}

/// Create Shaula's config file using atomic directory creation and explicit
/// overwrite semantics so the same operation can be safely exposed by UI later.
pub fn initConfig(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, options: InitOptions) !InitResult {
    const path = try defaultConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const exists = pathExists(io, path);
    if (exists and !options.overwrite) {
        return .{
            .path = path,
            .created = false,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    if (!options.dry_run) {
        try ensureParentDir(io, path);
        try writeFileAtomic(io, path, default_config_toml);
    }

    return .{
        .path = path,
        .created = !exists,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

pub fn defaultNiriConfigPath(allocator: std.mem.Allocator, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("NIRI_CONFIG")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }
    if (environ.getPosix("XDG_CONFIG_HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return std.fmt.allocPrint(allocator, "{s}/niri/config.kdl", .{raw});
    }
    if (environ.getPosix("HOME")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return std.fmt.allocPrint(allocator, "{s}/.config/niri/config.kdl", .{raw});
    }
    return error.ConfigUnreadable;
}

/// Install or replace Shaula's managed Niri window-rule block.
///
/// Contract constraints:
/// - only text between the Shaula markers is replaced.
/// - malformed Shaula markers fail with ConfigInvalid before backup/write.
/// - existing files get a timestamped backup before mutation.
/// - repeated installs with identical content are no-ops.
pub fn installNiriRule(
    allocator: std.mem.Allocator,
    io: std.Io,
    environ: std.process.Environ,
    kdl: []const u8,
    options: InstallOptions,
) !InstallResult {
    const path = if (options.path_override) |override|
        try allocator.dupe(u8, override)
    else
        try defaultNiriConfigPath(allocator, environ);
    errdefer allocator.free(path);

    const current = if (pathExists(io, path))
        try std.Io.Dir.cwd().readFileAlloc(io, path, allocator, .limited(1024 * 1024))
    else
        try allocator.dupe(u8, "");
    defer allocator.free(current);

    const block = try managedBlock(allocator, kdl);
    defer allocator.free(block);

    const replacement = try replaceOrAppendManagedBlock(allocator, current, block);
    defer allocator.free(replacement.content);

    if (std.mem.eql(u8, current, replacement.content)) {
        return .{
            .path = path,
            .backup_path = null,
            .installed = true,
            .replaced = replacement.replaced,
            .changed = false,
            .dry_run = options.dry_run,
        };
    }

    var backup_path: ?[]u8 = null;
    if (!options.dry_run) {
        try ensureParentDir(io, path);
        if (current.len > 0) {
            backup_path = try backupExisting(allocator, io, path, current);
        }
        try writeFileAtomic(io, path, replacement.content);
    }

    return .{
        .path = path,
        .backup_path = backup_path,
        .installed = true,
        .replaced = replacement.replaced,
        .changed = true,
        .dry_run = options.dry_run,
    };
}

fn managedBlock(allocator: std.mem.Allocator, kdl: []const u8) ![]u8 {
    return std.fmt.allocPrint(allocator, "{s}\n{s}{s}{s}\n", .{
        managed_block_begin,
        kdl,
        if (std.mem.endsWith(u8, kdl, "\n")) "" else "\n",
        managed_block_end,
    });
}

const ManagedReplaceResult = struct {
    content: []u8,
    replaced: bool,
};

fn replaceOrAppendManagedBlock(allocator: std.mem.Allocator, current: []const u8, block: []const u8) !ManagedReplaceResult {
    const begin_count = countOccurrences(current, managed_block_begin);
    const end_count = countOccurrences(current, managed_block_end);

    if (begin_count == 0 and end_count == 0) {
        const separator = if (current.len == 0 or std.mem.endsWith(u8, current, "\n")) "" else "\n";
        return .{
            .content = try std.fmt.allocPrint(allocator, "{s}{s}\n{s}", .{ current, separator, block }),
            .replaced = false,
        };
    }

    if (begin_count != 1 or end_count != 1) return error.ConfigInvalid;

    const begin = std.mem.indexOf(u8, current, managed_block_begin).?;
    const end = std.mem.indexOf(u8, current, managed_block_end).?;
    if (end < begin) return error.ConfigInvalid;

    var end_after = end + managed_block_end.len;
    if (end_after < current.len and current[end_after] == '\n') {
        end_after += 1;
    }
    const prefix = current[0..begin];
    const suffix = current[end_after..];
    return .{
        .content = try std.fmt.allocPrint(allocator, "{s}{s}{s}", .{ prefix, block, suffix }),
        .replaced = true,
    };
}

fn countOccurrences(haystack: []const u8, needle: []const u8) usize {
    var count: usize = 0;
    var cursor: usize = 0;
    while (std.mem.indexOfPos(u8, haystack, cursor, needle)) |index| {
        count += 1;
        cursor = index + needle.len;
    }
    return count;
}

fn backupExisting(allocator: std.mem.Allocator, io: std.Io, path: []const u8, current: []const u8) ![]u8 {
    const millis = std.Io.Timestamp.now(io, .real).toMilliseconds();
    return backupExistingWithBase(allocator, io, path, current, millis);
}

fn backupExistingWithBase(allocator: std.mem.Allocator, io: std.Io, path: []const u8, current: []const u8, base: i64) ![]u8 {
    var attempt: usize = 0;
    while (attempt < max_backup_attempts) : (attempt += 1) {
        const backup_path = if (attempt == 0)
            try std.fmt.allocPrint(allocator, "{s}.shaula-backup-{d}", .{ path, base })
        else
            try std.fmt.allocPrint(allocator, "{s}.shaula-backup-{d}-{d}", .{ path, base, attempt });
        errdefer allocator.free(backup_path);

        writeFileExclusive(io, backup_path, current) catch |err| switch (err) {
            error.PathAlreadyExists => {
                allocator.free(backup_path);
                continue;
            },
            else => return err,
        };
        return backup_path;
    }
    return error.ConfigUnreadable;
}

fn writeFileExclusive(io: std.Io, path: []const u8, contents: []const u8) !void {
    var file = if (std.fs.path.isAbsolute(path))
        try std.Io.Dir.createFileAbsolute(io, path, .{ .exclusive = true })
    else
        try std.Io.Dir.cwd().createFile(io, path, .{ .exclusive = true });
    var file_open = true;
    defer if (file_open) file.close(io);

    var buffer: [4096]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll(contents);
    try writer.interface.flush();
    file.close(io);
    file_open = false;
}

fn pathExists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
}

fn ensureParentDir(io: std.Io, path: []const u8) !void {
    if (std.fs.path.dirname(path)) |parent| {
        try std.Io.Dir.cwd().createDirPath(io, parent);
    }
}

fn writeFileAtomic(io: std.Io, path: []const u8, contents: []const u8) !void {
    const tmp_path = try std.fmt.allocPrint(std.heap.page_allocator, "{s}.tmp", .{path});
    defer std.heap.page_allocator.free(tmp_path);

    var file = if (std.fs.path.isAbsolute(tmp_path))
        try std.Io.Dir.createFileAbsolute(io, tmp_path, .{ .truncate = true })
    else
        try std.Io.Dir.cwd().createFile(io, tmp_path, .{ .truncate = true });
    var file_open = true;
    defer if (file_open) file.close(io);

    var buffer: [4096]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll(contents);
    try writer.interface.flush();
    file.close(io);
    file_open = false;

    if (std.fs.path.isAbsolute(tmp_path) and std.fs.path.isAbsolute(path)) {
        try std.Io.Dir.renameAbsolute(tmp_path, path, io);
    } else {
        try std.Io.Dir.cwd().rename(tmp_path, std.Io.Dir.cwd(), path, io);
    }
}

test "managed block appends when missing" {
    const block = try managedBlock(std.testing.allocator, "window-rule {}\n");
    defer std.testing.allocator.free(block);
    const result = try replaceOrAppendManagedBlock(std.testing.allocator, "input\n", block);
    defer std.testing.allocator.free(result.content);
    try std.testing.expect(!result.replaced);
    try std.testing.expect(std.mem.indexOf(u8, result.content, managed_block_begin) != null);
}

test "managed block replaces existing block only" {
    const old =
        \\before
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\old
        \\// END SHAULA PREVIEW WINDOW RULE
        \\after
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    const result = try replaceOrAppendManagedBlock(std.testing.allocator, old, block);
    defer std.testing.allocator.free(result.content);
    try std.testing.expect(result.replaced);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "before") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "after") != null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "old") == null);
    try std.testing.expect(std.mem.indexOf(u8, result.content, "new") != null);
}

test "managed block replacement is idempotent" {
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    const first = try replaceOrAppendManagedBlock(std.testing.allocator, "input\n", block);
    defer std.testing.allocator.free(first.content);
    const second = try replaceOrAppendManagedBlock(std.testing.allocator, first.content, block);
    defer std.testing.allocator.free(second.content);
    try std.testing.expectEqualStrings(first.content, second.content);
}

test "managed block rejects begin without end" {
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, managed_block_begin, block));
}

test "managed block rejects end without begin" {
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, managed_block_end, block));
}

test "managed block rejects end before begin" {
    const current =
        \\// END SHAULA PREVIEW WINDOW RULE
        \\body
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block));
}

test "managed block rejects duplicate begin markers" {
    const current =
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\first
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\// END SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block));
}

test "managed block rejects duplicate end markers" {
    const current =
        \\// BEGIN SHAULA PREVIEW WINDOW RULE
        \\first
        \\// END SHAULA PREVIEW WINDOW RULE
        \\// END SHAULA PREVIEW WINDOW RULE
        \\
    ;
    const block = try managedBlock(std.testing.allocator, "new\n");
    defer std.testing.allocator.free(block);
    try std.testing.expectError(error.ConfigInvalid, replaceOrAppendManagedBlock(std.testing.allocator, current, block));
}

test "backup creation never overwrites existing backup path" {
    const io = std.testing.io;
    const source_path = "/tmp/shaula-manager-backup-test.kdl";
    const existing_backup = "/tmp/shaula-manager-backup-test.kdl.shaula-backup-42";
    const expected_backup = "/tmp/shaula-manager-backup-test.kdl.shaula-backup-42-1";

    std.Io.Dir.deleteFileAbsolute(io, source_path) catch {};
    std.Io.Dir.deleteFileAbsolute(io, existing_backup) catch {};
    std.Io.Dir.deleteFileAbsolute(io, expected_backup) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, source_path) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, existing_backup) catch {};
    defer std.Io.Dir.deleteFileAbsolute(io, expected_backup) catch {};

    try writeFileAtomic(io, existing_backup, "existing");
    const backup_path = try backupExistingWithBase(std.testing.allocator, io, source_path, "new", 42);
    defer std.testing.allocator.free(backup_path);

    try std.testing.expectEqualStrings(expected_backup, backup_path);

    const existing_contents = try std.Io.Dir.cwd().readFileAlloc(io, existing_backup, std.testing.allocator, .limited(1024));
    defer std.testing.allocator.free(existing_contents);
    try std.testing.expectEqualStrings("existing", existing_contents);

    const backup_contents = try std.Io.Dir.cwd().readFileAlloc(io, expected_backup, std.testing.allocator, .limited(1024));
    defer std.testing.allocator.free(backup_contents);
    try std.testing.expectEqualStrings("new", backup_contents);
}
