const std = @import("std");
const spike_probe = @import("spike_probe.zig");

pub fn main(init: std.process.Init) !u8 {
    return spike_probe.run(init.gpa, init.io, init.minimal.environ);
}
