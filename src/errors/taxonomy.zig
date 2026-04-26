const std = @import("std");

pub const FailureClass = enum {
    cli,
    compositor,
    ipc,
    backend,
    clipboard,
    output,
    unknown,

    pub fn asString(class: FailureClass) []const u8 {
        return switch (class) {
            .cli => "cli",
            .compositor => "compositor",
            .ipc => "ipc",
            .backend => "backend",
            .clipboard => "clipboard",
            .output => "output",
            .unknown => "unknown",
        };
    }
};

pub const RecoveryAction = enum {
    fail_fast,
    retry_limited,
    degrade_continue,
    degrade_to_portal,

    pub fn asString(action: RecoveryAction) []const u8 {
        return switch (action) {
            .fail_fast => "fail_fast",
            .retry_limited => "retry_limited",
            .degrade_continue => "degrade_continue",
            .degrade_to_portal => "degrade_to_portal",
        };
    }
};

pub const ErrorSpec = struct {
    code: []const u8,
    message: []const u8,
    retryable: bool,
    class: FailureClass,
    action: RecoveryAction,
    exit_code: u8,
};

const specs = [_]ErrorSpec{
    .{ .code = "ERR_CLI_USAGE", .message = "invalid CLI usage", .retryable = false, .class = .cli, .action = .fail_fast, .exit_code = 2 },
    .{ .code = "ERR_UNSUPPORTED_COMPOSITOR", .message = "unsupported compositor for shaula v1", .retryable = false, .class = .compositor, .action = .fail_fast, .exit_code = 10 },
    .{ .code = "ERR_PREFLIGHT_ENV_NOT_READY", .message = "wayland environment is not ready", .retryable = true, .class = .compositor, .action = .retry_limited, .exit_code = 11 },
    .{ .code = "ERR_DAEMON_ALREADY_RUNNING", .message = "daemon already running", .retryable = false, .class = .ipc, .action = .fail_fast, .exit_code = 20 },
    .{ .code = "ERR_DAEMON_NOT_RUNNING", .message = "daemon is not running", .retryable = false, .class = .ipc, .action = .fail_fast, .exit_code = 21 },
    .{ .code = "ERR_IPC_BIND_FAILED", .message = "failed to bind daemon IPC socket", .retryable = false, .class = .ipc, .action = .retry_limited, .exit_code = 22 },
    .{ .code = "ERR_IPC_TIMEOUT", .message = "IPC operation timed out", .retryable = true, .class = .ipc, .action = .retry_limited, .exit_code = 23 },
    .{ .code = "ERR_IPC_REQUEST_INVALID", .message = "invalid or unsupported IPC request", .retryable = false, .class = .ipc, .action = .fail_fast, .exit_code = 24 },
    .{ .code = "ERR_CAPTURE_BACKEND_UNAVAILABLE", .message = "capture backend unavailable", .retryable = true, .class = .backend, .action = .degrade_to_portal, .exit_code = 30 },
    .{ .code = "ERR_WINDOW_TARGET_UNRESOLVED", .message = "window target could not be resolved", .retryable = false, .class = .backend, .action = .degrade_continue, .exit_code = 31 },
    .{ .code = "ERR_CAPTURE_TIMEOUT", .message = "capture operation timed out", .retryable = true, .class = .backend, .action = .retry_limited, .exit_code = 32 },
    .{ .code = "ERR_CAPTURE_PRECONDITION_TIMEOUT", .message = "capture precondition timed out waiting for shell artifact guard", .retryable = true, .class = .backend, .action = .retry_limited, .exit_code = 35 },
    .{ .code = "ERR_SELECTION_CANCELLED", .message = "selection was cancelled by user", .retryable = false, .class = .backend, .action = .fail_fast, .exit_code = 33 },
    .{ .code = "ERR_CAPTURE_MODE_UNSUPPORTED", .message = "capture mode is unsupported by runtime capabilities", .retryable = false, .class = .backend, .action = .fail_fast, .exit_code = 34 },
    .{ .code = "ERR_PREVIOUS_AREA_UNAVAILABLE", .message = "previous area is unavailable", .retryable = false, .class = .backend, .action = .fail_fast, .exit_code = 39 },
    .{ .code = "ERR_CLIPBOARD_UNAVAILABLE", .message = "clipboard backend is unavailable", .retryable = false, .class = .clipboard, .action = .degrade_continue, .exit_code = 40 },
    .{ .code = "ERR_OVERLAY_UNAVAILABLE", .message = "overlay helper is unavailable", .retryable = true, .class = .backend, .action = .retry_limited, .exit_code = 36 },
    .{ .code = "ERR_OVERLAY_TIMEOUT", .message = "overlay helper timed out", .retryable = true, .class = .backend, .action = .retry_limited, .exit_code = 37 },
    .{ .code = "ERR_OVERLAY_PROTOCOL_INVALID", .message = "overlay helper produced invalid protocol payload", .retryable = false, .class = .backend, .action = .fail_fast, .exit_code = 38 },
    .{ .code = "ERR_CLIPBOARD_IMPORT_INVALID", .message = "clipboard image import failed", .retryable = false, .class = .clipboard, .action = .fail_fast, .exit_code = 41 },
    .{ .code = "ERR_CLIPBOARD_COPY_FAILED", .message = "clipboard image copy failed", .retryable = false, .class = .clipboard, .action = .fail_fast, .exit_code = 42 },
    .{ .code = "ERR_HISTORY_STORE_UNAVAILABLE", .message = "history store unavailable", .retryable = false, .class = .output, .action = .degrade_continue, .exit_code = 50 },
    .{ .code = "ERR_HISTORY_ENTRY_NOT_FOUND", .message = "history entry was not found", .retryable = false, .class = .output, .action = .fail_fast, .exit_code = 52 },
    .{ .code = "ERR_OUTPUT_PATH_INVALID", .message = "output path is not writable", .retryable = false, .class = .output, .action = .fail_fast, .exit_code = 51 },
    .{ .code = "ERR_UNKNOWN_UNMAPPED", .message = "unmapped internal failure class", .retryable = false, .class = .unknown, .action = .fail_fast, .exit_code = 99 },
};

pub fn list() []const ErrorSpec {
    return &specs;
}

pub fn find(code: []const u8) ?ErrorSpec {
    for (specs) |spec| {
        if (std.mem.eql(u8, spec.code, code)) return spec;
    }
    return null;
}

pub fn unknownSpec() ErrorSpec {
    return specs[specs.len - 1];
}

test "taxonomy list contains ERR entries" {
    const all = list();
    try std.testing.expect(all.len > 0);
    for (all) |spec| {
        try std.testing.expect(std.mem.startsWith(u8, spec.code, "ERR_"));
    }
}
