const std = @import("std");

pub const backend_niri_wayland_direct = "niri-wayland-direct";
pub const backend_grim_wlroots = "grim-wlroots";
pub const backend_portal_screenshot = "portal-screenshot";
pub const backend_stub = "__stub__";

pub const warning_portal_fallback = "portal_fallback";
pub const warning_capture_backend_degraded = "capture_backend_degraded";
pub const warning_window_capture_degraded = "window_capture_degraded";

pub const helper_exit_ipc_timeout: u8 = 23;
pub const helper_exit_selection_cancelled: u8 = 33;
pub const helper_exit_unknown_unmapped: u8 = 99;

/// Shared backend contract tokens for Wayland capture execution.
///
/// Contract constraints: labels are public JSON values and helper exit codes
/// mirror `native_portal_screenshot.c`; changing either requires updating QA
/// expectations and deterministic `ERR_*` mapping together.
pub fn labelIsPortal(label: []const u8) bool {
    return std.mem.eql(u8, label, backend_portal_screenshot);
}

pub fn labelIsStub(label: []const u8) bool {
    return std.mem.eql(u8, label, backend_stub);
}

pub fn runtimeErrorForExitCode(code: u8) anyerror {
    return switch (code) {
        helper_exit_ipc_timeout => error.IpcTimeout,
        helper_exit_selection_cancelled => error.SelectionCancelled,
        helper_exit_unknown_unmapped => error.UnknownUnmapped,
        else => error.BackendUnavailable,
    };
}
