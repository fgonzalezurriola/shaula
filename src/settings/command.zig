const std = @import("std");

const cli_json = @import("../cli/json.zig");
const recovery_policy = @import("../recovery/policy.zig");

/// Launches the native GTK settings helper.
///
/// Contract constraints:
/// - Noctalia and users invoke `shaula settings`; GTK stays isolated in the
///   sibling helper binary.
/// - helper resolution prefers `SHAULA_SETTINGS_HELPER_BIN`, then the installed
///   sibling `shaula-settings`, then PATH.
/// - spawn/wait failures map to deterministic `ERR_SETTINGS_UNAVAILABLE`.
pub fn run(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ, argv: []const [*:0]const u8) !u8 {
    if (argv.len > 2) {
        try cli_json.writeBasicError(io, "settings", "ERR_CLI_USAGE", "usage: shaula settings", false);
        return recovery_policy.exitCodeFor("ERR_CLI_USAGE");
    }

    const helper_bin = try resolveSettingsBinary(allocator, io, environ);
    defer allocator.free(helper_bin);
    const shaula_bin = try std.process.executablePathAlloc(io, allocator);
    defer allocator.free(shaula_bin);

    var child = std.process.spawn(io, .{
        .argv = &.{ helper_bin, shaula_bin },
        .stdin = .ignore,
        .stdout = .ignore,
        .stderr = .ignore,
    }) catch {
        try cli_json.writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper is unavailable", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    };

    const term = child.wait(io) catch {
        try cli_json.writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper is unavailable", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    };

    const ok = switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
    if (!ok) {
        try cli_json.writeBasicError(io, "settings", "ERR_SETTINGS_UNAVAILABLE", "settings helper failed", true);
        return recovery_policy.exitCodeFor("ERR_SETTINGS_UNAVAILABLE");
    }

    return 0;
}

fn resolveSettingsBinary(allocator: std.mem.Allocator, io: std.Io, environ: std.process.Environ) ![]u8 {
    if (environ.getPosix("SHAULA_SETTINGS_HELPER_BIN")) |raw_z| {
        const raw = std.mem.trim(u8, std.mem.sliceTo(raw_z, 0), " \t\r\n");
        if (raw.len > 0) return allocator.dupe(u8, raw);
    }

    const exe_dir = std.process.executableDirPathAlloc(io, allocator) catch return allocator.dupe(u8, "shaula-settings");
    defer allocator.free(exe_dir);

    const sibling = try std.fmt.allocPrint(allocator, "{s}/shaula-settings", .{exe_dir});
    if (std.Io.Dir.accessAbsolute(io, sibling, .{})) {
        return sibling;
    } else |_| {
        allocator.free(sibling);
        return allocator.dupe(u8, "shaula-settings");
    }
}
