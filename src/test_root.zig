pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");

const _main = @import("main.zig");
const _capture_command_test = @import("capture/command_test.zig");
const _capture_backend_test = @import("backends/capture_backend_test.zig");
const _overlay_runtime = @import("overlay/runtime.zig");
const _raylib = @import("raylib");
const _clay = @import("clay");

test {
    _ = _main;
    _ = _capture_command_test;
    _ = _capture_backend_test;
    _ = _overlay_runtime;
    _ = _raylib;
    _ = _clay;
}
