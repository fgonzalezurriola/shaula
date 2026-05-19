const selection = @import("../selection/selection.zig");
const selection_session = @import("selection_session.zig");
const selection_draft_store = @import("selection_draft_store.zig");

pub const DraftMode = selection_draft_store.DraftMode;
pub const RegionCaptureMode = @import("../core/capture_mode.zig").RegionCaptureMode;
pub const deterministicFailureCode = selection_session.deterministicFailureCode;
pub const InteractionMode = selection_session.InteractionMode;
pub const FrozenSource = selection_session.FrozenSource;
pub const PreparedFrozenSource = selection_session.PreparedFrozenSource;
pub const SelectionOutcome = selection_session.SelectionOutcome;
pub const prepareFrozenSourceForOverlay = selection_session.prepareFrozenSourceForOverlay;

/// Public overlay selection facade used by capture lifecycle callers.
///
/// Contract constraint: callers cross one overlay seam; helper process setup,
/// helper protocol mapping, draft persistence, and toolbar state remain behind
/// the selection session implementation.
pub const runSelection = selection_session.runSelection;
pub const SelectionMode = selection.SelectionMode;
