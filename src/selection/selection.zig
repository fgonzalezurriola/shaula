const std = @import("std");

pub const SelectionMode = enum {
    freeform,
    window,
    output,
};

pub const SelectionConstraint = struct {
    aspect: ?[]const u8 = null,
};

pub const Geometry = struct {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
};

pub const SelectionResult = struct {
    mode: SelectionMode,
    aspect: ?[]const u8,
    geometry: ?Geometry,
    cancelled: bool,
};
