const std = @import("std");
const state_machine = @import("state_machine.zig");

test "state machine allows deterministic lifecycle transitions" {
    var sm = state_machine.StateMachine.init();
    try std.testing.expectEqual(state_machine.State.initializing, sm.current());

    try sm.transition(.ready);
    try std.testing.expectEqual(state_machine.State.ready, sm.current());

    try sm.transition(.capturing);
    try std.testing.expectEqual(state_machine.State.capturing, sm.current());

    try sm.transition(.degraded);
    try std.testing.expectEqual(state_machine.State.degraded, sm.current());

    try sm.transition(.ready);
    try std.testing.expectEqual(state_machine.State.ready, sm.current());
}

test "invalid transition is rejected" {
    var sm = state_machine.StateMachine.init();
    try std.testing.expectError(error.InvalidTransition, sm.transition(.capturing));
}
