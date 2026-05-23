const std = @import("std");
const c_compat = @import("c_compat");

const CInt = c_int;
const TRUE: CInt = 1;
const FALSE: CInt = 0;

extern fn g_free(mem: ?*anyopaque) void;
extern fn g_file_read_link(filename: [*:0]const u8, err: ?*?*anyopaque) ?[*:0]u8;
extern fn g_path_get_dirname(file_name: [*:0]const u8) ?[*:0]u8;
extern fn g_build_filename(first: [*:0]const u8, second: [*:0]const u8, sentinel: ?*anyopaque) ?[*:0]u8;
extern fn g_file_test(filename: [*:0]const u8, test_flags: CInt) CInt;
extern fn g_spawn_sync(
    working_directory: ?[*:0]const u8,
    argv: [*:null]?[*:0]u8,
    envp: ?[*:null]?[*:0]u8,
    flags: CInt,
    child_setup: ?*const anyopaque,
    user_data: ?*anyopaque,
    standard_output: ?*?[*:0]u8,
    standard_error: ?*?[*:0]u8,
    wait_status: *c_int,
    err: ?*?*anyopaque,
) CInt;
extern fn g_shell_quote(unquoted_string: [*:0]const u8) ?[*:0]u8;
extern fn g_quark_from_static_string(string: [*:0]const u8) c_uint;
extern fn g_set_error_literal(err: ?*?*anyopaque, domain: c_uint, code: CInt, message: [*:0]const u8) void;

const G_FILE_TEST_IS_EXECUTABLE: CInt = 1 << 3;
const G_SPAWN_SEARCH_PATH: CInt = 1 << 2;

export fn shaula_clipboard_copy_png_file(path_z: ?[*:0]const u8, err: ?*?*anyopaque) CInt {
    const path = if (path_z) |value| value else {
        setError(err, 1, "missing PNG path");
        return FALSE;
    };
    if (std.mem.span(path).len == 0) {
        setError(err, 1, "missing PNG path");
        return FALSE;
    }

    const shaula = resolveShaulaCli();
    defer g_free(shaula);
    var stderr_text: ?[*:0]u8 = null;
    defer g_free(stderr_text);
    var status: CInt = 1;
    var argv = [_:null]?[*:0]u8{ shaula, @constCast("clipboard"), @constCast("copy-image"), @constCast("--input"), @constCast(path), @constCast("--json") };
    if (g_spawn_sync(null, &argv, null, G_SPAWN_SEARCH_PATH, null, null, null, &stderr_text, &status, err) == FALSE) return FALSE;
    if (!c_compat.exitedZero(status)) {
        setError(err, 2, "shaula clipboard copy-image failed");
        return FALSE;
    }
    return TRUE;
}

export fn shaula_clipboard_copy_text(text_z: ?[*:0]const u8, err: ?*?*anyopaque) CInt {
    const text = text_z orelse @constCast("");
    const quoted = g_shell_quote(text) orelse return FALSE;
    defer g_free(quoted);
    const shell_cmd = c_compat.allocPrintZ("printf %s {s} | wl-copy --type text/plain", .{std.mem.span(quoted)});
    defer g_free(shell_cmd);
    var status: CInt = 1;
    var argv = [_:null]?[*:0]u8{ @constCast("/bin/sh"), @constCast("-c"), shell_cmd };
    if (g_spawn_sync(null, &argv, null, G_SPAWN_SEARCH_PATH, null, null, null, null, &status, err) == FALSE) return FALSE;
    if (!c_compat.exitedZero(status)) {
        setError(err, 3, "wl-copy text failed");
        return FALSE;
    }
    return TRUE;
}

fn resolveShaulaCli() [*:0]u8 {
    if (g_file_read_link("/proc/self/exe", null)) |exe| {
        defer g_free(exe);
        const dir = g_path_get_dirname(exe);
        if (dir) |value| {
            defer g_free(value);
            const candidate = g_build_filename(value, "shaula", null);
            if (candidate) |shaula| {
                if (g_file_test(shaula, G_FILE_TEST_IS_EXECUTABLE) == TRUE) return shaula;
                g_free(shaula);
            }
        }
    }
    return c_compat.dupZ("shaula");
}

fn setError(err: ?*?*anyopaque, code: CInt, message: [*:0]const u8) void {
    g_set_error_literal(err, g_quark_from_static_string("shaula-preview-clipboard"), code, message);
}
