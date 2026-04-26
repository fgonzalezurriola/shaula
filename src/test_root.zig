pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");

const _main = @import("main.zig");
const _capture_command_test = @import("capture/command_test.zig");
const _capture_backend_test = @import("backends/capture_backend_test.zig");
const _overlay_runtime = @import("overlay/runtime.zig");
const _overlay_toolbar_layout = @import("overlay/toolbar_layout.zig");
const _overlay_ui_state_store = @import("overlay/ui_state_store.zig");
const _overlay_all_in_one_session = @import("overlay/all_in_one_session.zig");
const _overlay_strategy = @import("overlay/strategy.zig");
const _raylib = @import("raylib");
const _clay = @import("clay");

test {
    _ = _main;
    _ = _capture_command_test;
    _ = _capture_backend_test;
    _ = _overlay_runtime;
    _ = _overlay_toolbar_layout;
    _ = _overlay_ui_state_store;
    _ = _overlay_all_in_one_session;
    _ = _overlay_strategy;
    _ = _raylib;
    _ = _clay;
}
