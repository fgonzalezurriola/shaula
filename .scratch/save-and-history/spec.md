# Save configuration and Preview history affordances

Migrate the remaining save-configuration and history-interface product work
from the roadmap. These are independent slices and should preserve the current
save, modified-state, undo, and redo contracts.

Source: `docs/roadmap.md`.

The first additional history affordance is action-aware Undo and Redo text. It
should tell the user which document operation will be undone or redone without
exposing snapshot implementation details. A navigable history list is deferred.
