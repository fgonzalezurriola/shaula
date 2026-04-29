const std = @import("std");

extern fn shaula_native_gtk_overlay_run() c_int;

/// Runs the native GTK/layer-shell overlay helper.
///
/// Contract constraint: the C runtime boundary emits only helper envelope v1
/// JSON on stdout, preserving deterministic `ERR_*` handling in the parent.
pub fn run() u8 {
    const rc = shaula_native_gtk_overlay_run();
    if (rc < 0) return 36;
    return @intCast(@min(rc, 255));
}
