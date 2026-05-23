const std = @import("std");
const c_compat = @import("c_compat");

const CInt = c_int;
const TRUE: CInt = 1;
const FALSE: CInt = 0;

extern fn g_free(mem: ?*anyopaque) void;
extern fn g_file_get_contents(filename: [*:0]const u8, contents: *?[*]u8, length: *usize, err: ?*?*anyopaque) CInt;
extern fn g_file_set_contents(filename: [*:0]const u8, contents: [*]const u8, length: isize, err: ?*?*anyopaque) CInt;
extern fn g_path_get_dirname(file_name: [*:0]const u8) ?[*:0]u8;
extern fn g_shell_quote(unquoted_string: [*:0]const u8) ?[*:0]u8;
extern fn g_spawn_command_line_async(command_line: [*:0]const u8, err: ?*?*anyopaque) CInt;

export fn shaula_image_io_copy_file_bytes(source: ?[*:0]const u8, target: ?[*:0]const u8, err: ?*?*anyopaque) CInt {
    if (source == null or target == null) return FALSE;
    var contents: ?[*]u8 = null;
    var len: usize = 0;
    if (g_file_get_contents(source.?, &contents, &len, err) == FALSE) return FALSE;
    defer g_free(contents);
    return g_file_set_contents(target.?, contents.?, @intCast(len), err);
}

export fn shaula_image_io_path_has_png_extension(path_z: ?[*:0]const u8) CInt {
    const path = if (path_z) |value| std.mem.span(value) else return FALSE;
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return FALSE;
    return if (std.ascii.eqlIgnoreCase(path[dot..], ".png")) TRUE else FALSE;
}

export fn shaula_image_io_with_png_extension(path_z: ?[*:0]const u8) ?[*:0]u8 {
    const path = if (path_z) |value| std.mem.span(value) else return null;
    if (shaula_image_io_path_has_png_extension(path_z) == TRUE) return c_compat.dupZ(path);
    return c_compat.allocPrintZ("{s}.png", .{path});
}

export fn shaula_image_io_open_containing_folder(path_z: ?[*:0]const u8, err: ?*?*anyopaque) CInt {
    const path = if (path_z) |value| value else return FALSE;
    if (std.mem.span(path).len == 0) return FALSE;
    const dir = g_path_get_dirname(path) orelse return FALSE;
    defer g_free(dir);
    const quoted = g_shell_quote(dir) orelse return FALSE;
    defer g_free(quoted);
    const command = c_compat.allocPrintZ("xdg-open {s}", .{std.mem.span(quoted)});
    defer g_free(command);
    return g_spawn_command_line_async(command, err);
}
