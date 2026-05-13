const std = @import("std");

const capture_types = @import("types.zig");
const core_capture_mode = @import("../core/capture_mode.zig");
const flags = @import("command_flags.zig");

fn backendRequestMode(mode: core_capture_mode.CaptureMode) capture_types.CaptureMode {
    return switch (core_capture_mode.runtimeMode(mode)) {
        .area => .area,
        .current_output => .focused,
        .all_outputs => .all_screens,
        .window => .window,
    };
}

pub const PostCaptureFlags = struct {
    save: bool,
    copy: bool,
    preview: bool,
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
    settle_region_mode: ?core_capture_mode.RegionCaptureMode = null,
};

/// Build the shared lifecycle invocation for one resolved capture command.
///
/// Contract constraint: public CLI mode tokens stay separate from backend modes
/// so `previous-area` and `all-in-one` can continue executing on the area lane.
pub fn area(parsed: flags.AreaFlags, region_capture_mode: core_capture_mode.RegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const mode = core_capture_mode.cliToken(.area);
    return .{
        .command = "capture area",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(mode, parsed.save, parsed.copy, parsed.preview),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn quick(parsed: flags.QuickFlags, region_capture_mode: core_capture_mode.RegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const mode = core_capture_mode.cliToken(.quick);
    return .{
        .command = "capture quick",
        .reported_mode = mode,
        .backend_mode = core_capture_mode.backendModeToken(.quick) orelse mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(mode, parsed.save, parsed.copy, parsed.preview),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn allInOne(parsed: flags.AllInOneFlags, region_capture_mode: core_capture_mode.RegionCaptureMode, geometry: ?capture_types.AreaGeometry) Invocation {
    const reported_mode = core_capture_mode.cliToken(.all_in_one);
    return .{
        .command = "capture all-in-one",
        .reported_mode = reported_mode,
        .backend_mode = core_capture_mode.backendModeToken(.all_in_one) orelse reported_mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(reported_mode, parsed.save, parsed.copy, parsed.preview),
        .persist_previous_area = geometry,
        .settle_region_mode = region_capture_mode,
    };
}

pub fn fullscreen(parsed: flags.FullscreenFlags) Invocation {
    const mode = core_capture_mode.cliToken(.fullscreen);
    return .{
        .command = "capture fullscreen",
        .reported_mode = mode,
        .backend_mode = core_capture_mode.backendModeToken(.fullscreen) orelse mode,
        .request_mode = backendRequestMode(.fullscreen),
        .output_path = parsed.output,
        .post_flags = postFlags(mode, parsed.save, parsed.copy, parsed.preview),
    };
}

pub fn allScreens(parsed: flags.AllScreensFlags) Invocation {
    const reported_mode = core_capture_mode.cliToken(.all_screens);
    return .{
        .command = "capture all-screens",
        .reported_mode = reported_mode,
        .backend_mode = core_capture_mode.backendModeToken(.all_screens) orelse reported_mode,
        .request_mode = backendRequestMode(.all_screens),
        .output_path = parsed.output,
        .post_flags = postFlags(reported_mode, parsed.save, parsed.copy, parsed.preview),
    };
}

pub fn focused(parsed: flags.FocusedFlags) Invocation {
    const mode = core_capture_mode.cliToken(.focused);
    return .{
        .command = "capture focused",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = backendRequestMode(.focused),
        .output_path = parsed.output,
        .post_flags = postFlags(mode, parsed.save, parsed.copy, parsed.preview),
    };
}

pub fn window(parsed: flags.WindowFlags) Invocation {
    const mode = core_capture_mode.cliToken(.window);
    return .{
        .command = "capture window",
        .reported_mode = mode,
        .backend_mode = mode,
        .request_mode = .window,
        .output_path = parsed.output,
        .window_id = parsed.window_id,
        .post_flags = postFlags(mode, parsed.save, parsed.copy, parsed.preview),
    };
}

pub fn previousArea(parsed: flags.PreviousAreaFlags, geometry: capture_types.AreaGeometry) Invocation {
    const reported_mode = core_capture_mode.cliToken(.previous_area);
    return .{
        .command = "capture previous-area",
        .reported_mode = reported_mode,
        .backend_mode = core_capture_mode.backendModeToken(.previous_area) orelse reported_mode,
        .request_mode = .area,
        .output_path = parsed.output,
        .area_geometry = geometry,
        .post_flags = postFlags(reported_mode, parsed.save, parsed.copy, parsed.preview),
        .persist_previous_area = geometry,
    };
}

pub fn postFlags(mode: []const u8, save: bool, copy: bool, preview: ?bool) PostCaptureFlags {
    return .{
        .save = save,
        .copy = copy,
        .preview = flags.resolvePreviewDefault(mode, preview),
    };
}
