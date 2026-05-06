# State

Snapshot for prompt reuse. `./dev state` copies this file, the last 3 commits,
and the working diff.

## Current focus

- Architecture deepening pass completed across the active capture, overlay, CLI
  contract, and preview surfaces.
- `src/cli/json.zig` is now the shared CLI JSON contract helper for timestamps,
  string/null encoding, warnings arrays, and basic `ERR_*` envelopes. Capture,
  daemon, clipboard, config, and capabilities JSON writers delegate common
  formatting there while preserving their existing command-specific response
  shapes.
- Runtime capture execution now resolves concrete helper/grim argv through
  `src/backends/capture_execution_plan.zig`. The process execution adapter still
  owns spawning, and backend callers still own deterministic taxonomy mapping.
- Overlay helper stdio contract parsing moved to
  `src/overlay/helper_protocol.zig`. `overlay.zig` now focuses on helper process
  setup, optional frozen-background preparation, draft/UI persistence, and
  selection orchestration.
- Preview command metadata is centralized in `preview_commands.c` through a
  command registry table for shortcuts, labels, and tool-command mapping.
- Preview document edit lifecycle is centralized in
  `preview_document_edit.{h,c}` for undo snapshot timing, image replacement,
  modified state, transient state cleanup, toolbar refresh, and redraw. Existing
  crop, blur, erase, and Spotlight algorithms remain in `preview_state.c`.

## Verification

- `./dev check` passed after the architecture refactor.
- `git diff --check` passed.
- Targeted checks passed: `./dev doctor`, `./dev strategies`, and `./dev bench`.
