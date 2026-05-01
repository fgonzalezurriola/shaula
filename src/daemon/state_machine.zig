const std = @import("std");
const protocol = @import("../ipc/protocol.zig");

pub const State = protocol.LifecycleState;

pub const TransitionError = error{InvalidTransition};

pub const StateMachine = struct {
    state: State,

    pub fn init() StateMachine {
        return .{ .state = .initializing };
    }

    pub fn current(self: *const StateMachine) State {
        return self.state;
    }

    pub fn transition(self: *StateMachine, next: State) TransitionError!void {
        if (!isAllowed(self.state, next)) {
            return error.InvalidTransition;
        }
        self.state = next;
    }

    fn isAllowed(from: State, to: State) bool {
        return switch (from) {
            .initializing => switch (to) {
                .ready, .degraded, .@"error" => true,
                else => false,
            },
            .ready => switch (to) {
                .capturing, .degraded, .@"error" => true,
                else => false,
            },
            .capturing => switch (to) {
                .ready, .degraded, .@"error" => true,
                else => false,
            },
            .degraded => switch (to) {
                .ready, .capturing, .@"error" => true,
                else => false,
            },
            .@"error" => switch (to) {
                .initializing => true,
                else => false,
            },
        };
    }
};
