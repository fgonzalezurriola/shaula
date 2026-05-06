pub const capture_types_module = @import("capture/types.zig");
pub const preflight_probe_module = @import("preflight/probe.zig");
pub const runtime_capabilities_module = @import("capabilities/runtime.zig");

const _capture_backend_test = @import("backends/capture_backend_test.zig");
const _capture_command_test = @import("capture/command_test.zig");
const _capture_command_flags_test = @import("capture/command_flags_test.zig");
const _capture_lifecycle = @import("capture/lifecycle.zig");
const _capture_types_test = @import("capture/types_test.zig");
const _config_loader = @import("config/loader.zig");
const _config_niri_rule = @import("config/niri_rule.zig");
const _config_manager = @import("config/manager.zig");
const _daemon_cli_control_test = @import("daemon/cli_control_test.zig");
const _daemon_state_machine_test = @import("daemon/state_machine_test.zig");
const _overlay_runtime = @import("overlay/runtime.zig");
const _overlay_toolbar_layout_test = @import("overlay/toolbar_layout_test.zig");
const _overlay_ui_state_store = @import("overlay/ui_state_store.zig");
const _overlay_all_in_one_session = @import("overlay/all_in_one_session.zig");
const _overlay_selection_draft_store = @import("overlay/selection_draft_store.zig");
const _preview_service = @import("preview/service.zig");
const _recovery_policy_test = @import("recovery/policy_test.zig");
const _runtime_process_exec = @import("runtime/process_exec.zig");

test {
    _ = _capture_backend_test;
    _ = _capture_command_test;
    _ = _capture_command_flags_test;
    _ = _capture_lifecycle;
    _ = _capture_types_test;
    _ = _config_loader;
    _ = _config_niri_rule;
    _ = _config_manager;
    _ = _daemon_cli_control_test;
    _ = _daemon_state_machine_test;
    _ = _overlay_runtime;
    _ = _overlay_toolbar_layout_test;
    _ = _overlay_ui_state_store;
    _ = _overlay_all_in_one_session;
    _ = _overlay_selection_draft_store;
    _ = _preview_service;
    _ = _recovery_policy_test;
    _ = _runtime_process_exec;
}
