const _main = @import("main.zig");
const _capture_command_test = @import("capture/command_test.zig");
const _capture_backend_test = @import("backends/capture_backend_test.zig");

test {
    _ = _main;
    _ = _capture_command_test;
    _ = _capture_backend_test;
}
