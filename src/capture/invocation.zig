const std = @import("std");

const capture_types = @import("types.zig");
const flags = @import("command_flags.zig");
const c = @cImport({
    @cInclude("core/capture_mode.h");
});

fn requiredSpan(value: c.ShaulaCaptureModeSpan) []const u8 {
    if (value.data == null) unreachable;
    return value.data[0..value.length];
}

fn optionalSpan(value: c.ShaulaCaptureModeSpan) ?[]const u8 {
    if (value.data == null) return null;
    return value.data[0..value.length];
}

fn cliToken(mode: c.ShaulaCaptureMode) []const u8 {
    return requiredSpan(c.shaula_capture_mode_cli_token(mode));
}

fn backendModeToken(mode: c.ShaulaCaptureMode) ?[]const u8 {
    return optionalSpan(c.shaula_capture_mode_backend_token(mode));
}

fn backendRequestMode(mode: c.ShaulaCaptureMode) capture_types.CaptureMode {
    return switch (c.shaula_capture_mode_runtime_mode(mode)) {
        c.SHAULA_RUNTIME_CAPTURE_MODE_AREA => .area,
        c.SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT => .focused,
        c.SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS => .all_screens,
        c.SHAULA_RUNTIME_CAPTURE_MODE_WINDOW => .window,
        else => unreachable,
    };
}

pub const PostCaptureFlags = struct {
    save: bool,
    copy: bool,
    preview: bool,
    save_explicit: bool = false,
    copy_explicit: bool = false,
    preview_explicit: bool = false,
    show_success_notifications: bool = true,
    show_error_notifications: bool = true,
    include_notification_thumbnail: bool = true,
};

pub const Invocation = struct {
    command: []const u8,
    reported_mode: []const u8,
    backend_mode: []const u8,
    request_mode: capture_types.CaptureMode,
    output_path: ?[]const u8 = null,
    window_id: ?[]const u8 = null,
    area_geometry: ?capture_types.AreaGeometry = null,
    post_flags: PostCaptureFlags,
    persist_previous_area: ?capture_types.AreaGeometry = null,
    settle_region_mode: ?c.ShaulaRegionCaptureMode = null,
};

/// Build the shared lifecycle invocation for one resolved capture command.
///
/// Contract constraint: public CLI mode tokens stay separate from backend modes
/// so `previous-area` and `all-in-one` can continue executing on the area lane.
pub fn area(parsed: flags.AreaFlags, region_capture_mode: c.ShaulaRegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const mode = cliToken(c.SHAULA_CAPTURE_MODE_AREA);
    return .{
        .command = "capture area",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(mode, parsed),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn quick(parsed: flags.QuickFlags, region_capture_mode: c.ShaulaRegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const mode = cliToken(c.SHAULA_CAPTURE_MODE_QUICK);
    return .{
        .command = "capture quick",
        .reported_mode = mode,
        .backend_mode = backendModeToken(c.SHAULA_CAPTURE_MODE_QUICK) orelse mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(mode, parsed),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn allInOne(parsed: flags.AllInOneFlags, region_capture_mode: c.ShaulaRegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const reported_mode = cliToken(c.SHAULA_CAPTURE_MODE_ALL_IN_ONE);
    return .{
        .command = "capture all-in-one",
        .reported_mode = reported_mode,
        .backend_mode = backendModeToken(c.SHAULA_CAPTURE_MODE_ALL_IN_ONE) orelse reported_mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(reported_mode, parsed),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn fullscreen(parsed: flags.FullscreenFlags) Invocation {
    const mode = cliToken(c.SHAULA_CAPTURE_MODE_FULLSCREEN);
    return .{
        .command = "capture fullscreen",
        .reported_mode = mode,
        .backend_mode = backendModeToken(c.SHAULA_CAPTURE_MODE_FULLSCREEN) orelse mode,
        .request_mode = backendRequestMode(c.SHAULA_CAPTURE_MODE_FULLSCREEN),
        .output_path = parsed.output,
        .post_flags = postFlags(mode, parsed),
    };
}

pub fn allScreens(parsed: flags.AllScreensFlags) Invocation {
    const reported_mode = cliToken(c.SHAULA_CAPTURE_MODE_ALL_SCREENS);
    return .{
        .command = "capture all-screens",
        .reported_mode = reported_mode,
        .backend_mode = backendModeToken(c.SHAULA_CAPTURE_MODE_ALL_SCREENS) orelse reported_mode,
        .request_mode = backendRequestMode(c.SHAULA_CAPTURE_MODE_ALL_SCREENS),
        .output_path = parsed.output,
        .post_flags = postFlags(reported_mode, parsed),
    };
}

pub fn focused(parsed: flags.FocusedFlags) Invocation {
    const mode = cliToken(c.SHAULA_CAPTURE_MODE_FOCUSED);
    return .{
        .command = "capture focused",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = backendRequestMode(c.SHAULA_CAPTURE_MODE_FOCUSED),
        .output_path = parsed.output,
        .post_flags = postFlags(mode, parsed),
    };
}

pub fn window(parsed: flags.WindowFlags) Invocation {
    const mode = cliToken(c.SHAULA_CAPTURE_MODE_WINDOW);
    return .{
        .command = "capture window",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = .window,
        .output_path = parsed.output,
        .window_id = parsed.window_id,
        .post_flags = postFlags(mode, parsed),
    };
}

pub fn previousArea(parsed: flags.PreviousAreaFlags, geometry: capture_types.AreaGeometry) Invocation {
    const reported_mode = cliToken(c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA);
    return .{
        .command = "capture previous-area",
        .reported_mode = reported_mode,
        .backend_mode = backendModeToken(c.SHAULA_CAPTURE_MODE_PREVIOUS_AREA) orelse reported_mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(reported_mode, parsed),
        .persist_previous_area = geometry,
    };
}

pub fn postFlags(mode: []const u8, parsed: anytype) PostCaptureFlags {
    return .{
        .save = parsed.save,
        .copy = parsed.copy,
        .preview = flags.resolvePreviewDefault(mode, parsed.preview),
        .save_explicit = if (@hasField(@TypeOf(parsed), "save_explicit")) parsed.save_explicit else false,
        .copy_explicit = if (@hasField(@TypeOf(parsed), "copy_explicit")) parsed.copy_explicit else false,
        .preview_explicit = if (@hasField(@TypeOf(parsed), "preview_explicit")) parsed.preview_explicit else false,
    };
}
