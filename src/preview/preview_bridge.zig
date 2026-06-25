//! Single Zig compilation root for the native preview helper.
//!
//! Importing every C ABI module here keeps their existing exports and behavior
//! while ensuring the linked helper contains one Zig runtime and one TLS state.

const geometry = @import("preview_geometry.zig");
const image_io = @import("preview_image_io.zig");
const clipboard = @import("preview_clipboard.zig");
const notify = @import("preview_notify.zig");

comptime {
    _ = geometry;
    _ = image_io;
    _ = clipboard;
    _ = notify;
}
