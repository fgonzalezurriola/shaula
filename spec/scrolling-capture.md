# Shaula Scrolling Capture Specification

See [spec/requirements.md](requirements.md) for product direction and
[spec/wayland-niri-integration.md](wayland-niri-integration.md) for the
Wayland/Niri runtime contract.

## 1. Summary

Scrolling capture produces a single tall image from content that extends
beyond the visible viewport, by capturing overlapping frames while the
content scrolls and then stitching them together. This spec defines the
full product contract, architecture, algorithms, and error taxonomy for
Shaula's scrolling capture on Wayland/Niri.

## 2. Primary Research Sources

| Tool | Platform | Approach | Stitching Algorithm | Key Takeaway |
|---|---|---|---|---|
| **Shottr** | macOS | Auto-scroll via Accessibility API; area selection then auto-scrolls down; reverse scroll supported | Proprietary; requires Accessibility permissions for scroll injection | Best UX reference: select area -> auto-scroll -> preview. Max height 200k px. |
| **jbonney/scrollshot** | Wayland (wlroots) | `zwlr_virtual_pointer_v1` for auto-scroll; `zwlr_screencopy_manager_v1` for frame capture; `zwlr_layer_shell_v1` for region selection overlay | Row-by-row voting: every row in `prev` matched against `next` via sampled SAD; most-voted scroll offset wins. Seam placed at lowest-SAD row in overlap middle 80%. | **Closest to Shaula's needs.** Pure Wayland, no external tools. Uses virtual pointer for scroll injection. Fallback to last good scroll on detection failure. |
| **wayscrollshot** | Wayland (wlroots) | Manual scroll by user; `grim` for capture; `slurp` for region selection; `smithay-client-toolkit` layer-shell preview | 5 algorithms: ORB+RANSAC (default via OpenCV), column-sample (3-region MAD with predicted-offset search), template (NCC), edge-detection, FAST+HNSW. Fallback chain: ORB -> relaxed ORB -> template. | **Richest stitching algorithm suite.** Real-time preview during capture. Column-sample is O(9*height) vs O(width*height). Manual scroll is safer but less magical. |
| **ShareX** | Windows | Auto-scroll via `SendInput` (mouse wheel / PageDown / arrow keys); region selection | Full-width SAD with side/bottom edge trimming; search bottom-up for maximum consecutive matching lines; "partially successful" fallback uses best-guess offset when exact match fails. | **Mature detection pipeline.** Trimming scrollbars (ignore side offset) and bottom edge (ignore bottom offset) is essential for reliability. Side trim = max(50, width/20). Bottom trim = max(50, height/20). |
| **Flameshot PR #4590** | X11/Wayland/Win | Auto-scroll; X11: xtest, Wayland: PipeWire+portal, Win: SendInput | OpenCV stitching (same approach as ShareX) | PR was rejected: too large, not engineered, added OpenCV dependency. Lesson: avoid heavy deps, design incrementally. |
| **Long_ScreenShot** | Python/X11 | Auto-scroll via xdotool; grayscale Euclidean distance minimization for stitching | Convert to grayscale, slide frames vertically, minimize Euclidean distance of overlapping area | Grayscale simplification works; insensitive to light watermarks; struggles with large blank areas. |
| **ScrollSnap (macOS)** | macOS | ScreenCaptureKit for capture; StitchingManager for overlap detection | Overlap detection (undetailed in README) | Shows that separating capture and stitching into distinct managers is a clean architecture pattern. |
| **wl-longshot** | Wayland (wlroots) | Three backends: grim (manual piece-by-piece + auto), wl-screenrec (stream recording), wf-recorder (fallback video) | Python OpenCV stitching for grim backend; stream-based: extract frames from video recording | **Stream-based approach is interesting:** record a video while user scrolls, then extract+stitch keyframes. Avoids timing issues entirely. |

## 3. Wayland/Niri Constraints

### 3.1 No compositor scroll API

Wayland has no protocol to programmatically scroll a surface. Two workarounds
exist:

| Method | Protocol | Works on Niri? | Trade-off |
|---|---|---|---|
| Virtual pointer (`zwlr_virtual_pointer_manager_v1`) | wlr-protocol | **Yes** (Niri implements wlroots protocols) | Requires protocol availability; scroll events may not reach the intended surface if focus is wrong |
| `ydotool` (uinput kernel virtual device) | Linux kernel | Yes (compositor-agnostic) | Requires `ydotool` daemon running; writes to `/dev/uinput`; root setup for daemon; more fragile |

**Decision**: Use `zwlr_virtual_pointer_manager_v1` as the primary scroll
method. This is a direct Wayland protocol, no external daemon, no root, and
Niri supports it. `ydotool` is documented as a future fallback only.

### 3.2 No off-screen content access

Wayland compositors do not expose content outside the visible viewport. The
only way to capture scrolled content is to actually scroll it into view and
capture what is on screen. This is fundamentally different from macOS
(Shottr can ask the system for window content) or browser extensions (DOM
access).

### 3.3 Frame capture path

Shaula's existing capture backend (`grim` or `zwlr_screencopy`) captures the
visible output. For scrolling capture we need `zwlr_screencopy_manager_v1`
with `capture_output_region` to capture just the selected region repeatedly,
without spawning an external process each time. This is the approach
jbonney/scrollshot uses and it avoids the overhead of forking `grim` in a
tight capture loop.

### 3.4 Overlay for region selection

The existing `zwlr_layer_shell_v1` overlay is reused for region selection.
The overlay hides before the capture loop starts (150ms settle delay as in
scrollshot) so it does not appear in captured frames.

### 3.5 Required Wayland protocols

| Protocol | Purpose | Required? |
|---|---|---|
| `zwlr_layer_shell_v1` | Region selection overlay | Yes |
| `zwlr_screencopy_manager_v1` | Frame capture | Yes |
| `zwlr_virtual_pointer_manager_v1` | Auto-scroll injection | Yes (auto mode) |
| `wl_seat` | Seat for virtual pointer creation | Yes |

If any required protocol is missing, scrolling capture emits
`ERR_SCROLLING_PROTOCOL_UNAVAILABLE` with the missing protocol name.

## 4. Product Contract

### 4.1 CLI grammar addition

```text
capture command += scrolling

shaula capture scrolling --json \
  [--copy] [--save] [--preview|--no-preview] \
  [--output <path>] \
  [--scroll-mode auto|manual] \
  [--scroll-direction down|up] \
  [--scroll-delay <ms>] \
  [--scroll-ticks <n>] \
  [--max-height <px>] \
  [--max-frames <n>] \
  [--stitch-algorithm row-vote|col-sample]
```

### 4.2 Defaults

| Parameter | Default | Rationale |
|---|---|---|
| `scroll-mode` | `auto` | Most user-friendly; falls back to manual on protocol error |
| `scroll-direction` | `down` | Matches reading direction |
| `scroll-delay` | `200` | jbonney/scrollshot default; balances speed vs render settle |
| `scroll-ticks` | `2` | jbonney/scrollshot default; 2 discrete wheel ticks per step |
| `max-height` | `20000` | Shottr's original default; configurable up to 200000 |
| `max-frames` | `200` | jbonney/scrollshot's limit; prevents runaway capture |
| `stitch-algorithm` | `row-vote` | Best accuracy for general content; `col-sample` for speed |

### 4.3 Config contract

```toml
[capture.scrolling]
scroll_mode = "auto"         # auto | manual
scroll_direction = "down"    # down | up
scroll_delay = 200           # ms between scroll and capture
scroll_ticks = 2             # virtual pointer wheel ticks per step
max_height = 20000           # px; hard cap at 200000
max_frames = 200             # stop after N frames
stitch_algorithm = "row-vote" # row-vote | col-sample
stabilize_attempts = 10      # initial frame stabilization probes
```

### 4.4 Capture flow (auto mode)

```
User invokes: shaula capture scrolling
         │
         ▼
┌─────────────────────┐
│ 1. Preflight check  │  protocols available? overlay ready?
└────────┬────────────┘
         │
         ▼
┌─────────────────────┐
│ 2. Region selection │  layer-shell overlay; user drags rectangle
└────────┬────────────┘
         │  overlay hides; 150ms settle
         ▼
┌─────────────────────┐
│ 3. First frame      │  capture output_region; stabilize loop
└────────┬────────────┘
         │
         ▼
┌──────────────────────────────────────┐
│ 4. Capture loop (repeat until stop): │
│   a. virtual_pointer.axis_discrete   │
│   b. sleep(scroll_delay)             │
│   c. capture output_region           │
│   d. compare with previous frame     │
│   e. if unchanged for N frames: STOP │
│   f. if max_height exceeded: STOP    │
│   g. if max_frames exceeded: STOP    │
│   h. append frame to buffer          │
└────────┬─────────────────────────────┘
         │
         ▼
┌─────────────────────┐
│ 5. Stitch frames    │  row-vote or col-sample algorithm
└────────┬────────────┘
         │
         ▼
┌─────────────────────┐
│ 6. Post-capture     │  save/copy/preview (same pipeline as other modes)
└─────────────────────┘
```

### 4.5 Capture flow (manual mode)

In manual mode, Shaula does not inject scroll events. The user scrolls
manually while Shaula captures frames at a regular interval (polling). This
is the wayscrollshot model: user scrolls, Shaula watches and stitches.

```
After region selection:
  1. Show control bar (layer-shell: Save / Copy / Pause / Cancel)
  2. Start periodic capture (every scroll_delay ms)
  3. Each captured frame is compared with previous:
     - If significant overlap detected: stitch into result
     - If identical: skip (no change)
     - If no overlap: skip (scrolled too far or too fast)
  4. Real-time preview of stitched result (optional)
  5. User presses Save/Copy or Shaula detects end-of-scroll
```

Manual mode is the safe fallback when virtual pointer is unavailable, and
is also useful when the user wants fine-grained control over scroll speed
or needs to scroll content that doesn't respond to wheel events (touchpad
gestures, custom scroll UI).

## 5. Stitching Algorithms

### 5.1 Row-Vote Algorithm (default)

Source: jbonney/scrollshot `stitch.rs`. Proven, well-tested, pure Zig
implementation with no external dependencies.

**Step 1 — Scroll offset detection:**

For every row `py` in `prev`, find the best-matching row `ny` in `next`
using sampled SAD (sum of absolute differences, sampling every 4th pixel).
Each match where `py > ny` implies a scroll offset `s = py - ny`. The most-
voted scroll offset wins. Content rows (unique text/graphics) vastly
outnumber ambiguous blank rows, so the correct offset wins the vote.

```
threshold = (width / 4) * 3   // avg SAD < 1.0 per sampled channel
MIN_SCROLL_VOTES = 5
```

If no offset reaches `MIN_SCROLL_VOTES`, fall back to the last good offset
or `frame_height / 4`.

Complexity: O(height² * width/4) for the pairwise scan.

**Step 2 — Seam finding:**

Within the overlap region, find the row where both frames are most pixel-
similar using full-width SAD. Search the middle 80% of the overlap (avoid
edges with capture artifacts). The seam row is where the cut is placed so
any rendering differences at the boundary are minimized.

```
overlap = frame_height - scroll_offset
margin = overlap / 10
search range = [margin, overlap - margin]
```

**Step 3 — Assembly:**

For each consecutive pair `(frame_i, frame_i+1)` with scroll `s` and seam
`r`:
- Frame `i` provides rows `[0, s + r)`
- Frame `i+1` provides rows `[r, frame_height)`

Total output height = `frame_height + sum(scrolls)`.

### 5.2 Column-Sample Algorithm (fast)

Source: wayscrollshot, inspired by screenshot-splicing.

Instead of comparing all rows, sample 3 column groups and search for
overlap using Mean Absolute Difference (MAD) on those columns only.

1. **Sample 3 column groups** from each frame:
   - Left: `[20, width/4)`
   - Middle: `[width/2, 5*width/8)`
   - Right: `[6*width/8, 7*width/8)`

2. **Convert to grayscale** and average each group.

3. **Search for overlap** starting from the predicted offset (based on
   previous scroll), expanding outward: `[p, p+1, p-1, p+2, p-2, ...]`

4. **Early termination** when MAD < threshold.

Complexity: O(9 * height) — significant speedup over row-vote for wide
images, at the cost of reduced accuracy on pages with similar column
content (e.g., card grids).

### 5.3 Algorithm selection guidance

| Content type | Recommended | Rationale |
|---|---|---|
| Text-heavy (docs, chat, code) | `row-vote` | Unique row content makes voting robust |
| Wide graphics / dashboards | `col-sample` | Faster; enough column diversity |
| Repeated elements (cards, tables) | `row-vote` | Column sampling fails on uniform grids |
| Very tall captures (>100 frames) | `col-sample` | Performance matters more at scale |

## 6. Edge-Case Handling

### 6.1 Static elements (fixed headers/footers)

Fixed/sticky elements appear in every frame and cause stitching artifacts
(duplicated headers in the output). This is a known unsolved problem across
all tools. ShareX and browser extensions handle it by hiding fixed elements
after the first capture, but that requires DOM access.

**Shaula approach**: Document the limitation. The user should select a
region that excludes fixed headers/footers when possible. Future: detect
and mask repeated regions in the overlap area (requires per-row duplication
detection across frames, which is additional complexity for v1).

### 6.2 Identical frames (end-of-scroll detection)

When consecutive frames are pixel-identical (diff < threshold), the content
has stopped scrolling. Shaula requires `STOP_STREAK = 2` consecutive
unchanged frames before stopping (matching scrollshot's approach) to avoid
false stops from lazy-loaded content.

### 6.3 Initial frame stabilization

The first frame may not be fully rendered (lazy images, font loading). After
the first capture, Shaula re-captures and compares until the frame
stabilizes (diff < `DIFF_THRESHOLD`), up to `stabilize_attempts` (default
10). This matches scrollshot's `STABILIZE_ATTEMPTS`.

### 6.4 Repeated content pages

Pages with many similar elements (identical cards, table rows) can cause
the row-vote algorithm to misalign. The `MIN_SCROLL_VOTES = 5` threshold
filters most noise, and the seam-finding step provides a second line of
defense by placing the cut at the most-similar row pair.

### 6.5 Large blank areas

Blank/whitespace regions look identical across frames and produce ambiguous
row matches. Row-vote handles this by having content rows outvote blank
rows. If a page is mostly blank, col-sample is worse (fewer discriminative
columns). Recommendation: row-vote for blank-heavy content.

### 6.6 Max-height cap

Output images are capped at `max_height` (default 20000px, configurable up
to 200000px). When the stitched result would exceed `max_height`, capture
stops and the partial result is returned with a `warnings` field indicating
truncation. This prevents OOM on very long pages.

### 6.7 Side and bottom edge trimming

From ShareX's hard-won experience: scrollbars and bottom-edge rendering
artifacts cause stitching failures. Shaula trims:

- **Side trim**: `max(50, width / 20)` pixels from left and right edges
  (excludes scrollbars)
- **Bottom trim**: `max(50, height / 20)` pixels from bottom (excludes
  partially rendered content at the viewport edge)

These trims apply only to the overlap detection search, not to the final
output image.

## 7. Error Taxonomy

| Error code | Condition |
|---|---|
| `ERR_SCROLLING_PROTOCOL_UNAVAILABLE` | Missing `zwlr_virtual_pointer_manager_v1` or `zwlr_screencopy_manager_v1` |
| `ERR_SCROLLING_NO_REGION` | User cancelled region selection |
| `ERR_SCROLLING_NO_FRAMES` | Capture produced zero frames |
| `ERR_SCROLLING_STITCH_FAILED` | No reliable scroll offset found between consecutive frames |
| `ERR_SCROLLING_MAX_HEIGHT` | Output would exceed max_height; partial result returned in `warnings` |
| `ERR_SCROLLING_MAX_FRAMES` | max_frames reached; partial result returned in `warnings` |
| `ERR_SCROLLING_TIMEOUT` | Frame capture did not complete within timeout |

## 8. JSON Output Contract

```json
{
  "ok": true,
  "contract_version": "1.0.0",
  "command": "capture scrolling",
  "timestamp": "2026-05-11T12:00:00Z",
  "result": {
    "mode": "auto",
    "frames_captured": 15,
    "output_dimensions": { "width": 1200, "height": 18500 },
    "scroll_offsets": [480, 478, 480, 479, ...],
    "stitch_algorithm": "row-vote",
    "artifact_path": "/tmp/shaula/captures/scroll_20260511_120000.png"
  },
  "warnings": []
}
```

On partial capture:

```json
{
  "ok": true,
  "warnings": ["SCROLLING_MAX_FRAMES: capture stopped after 200 frames"]
}
```

On stitch failure:

```json
{
  "ok": false,
  "error": {
    "code": "ERR_SCROLLING_STITCH_FAILED",
    "message": "no reliable scroll offset between frames 7 and 8"
  }
}
```

## 9. Architecture Integration

### 9.1 New modules

```
src/
  capture/
    scrolling_session.zig    # orchestrates: select -> capture loop -> stitch -> post-capture
    scrolling_capture.zig    # frame capture loop + virtual pointer scroll
    scrolling_stitch.zig     # row-vote + col-sample stitching algorithms
    scrolling_types.zig      # ScrollingConfig, ScrollingResult, FramePair, ScrollOffset
  backends/
    scrolling_execution_plan.zig  # backend operation for scrolling capture
```

### 9.2 Integration points

| Existing module | Change |
|---|---|
| `capture/command_grammar.zig` | Add `scrolling` command token and flags |
| `capture/command_flags.zig` | Add `ScrollingFlags` struct |
| `capture/invocation.zig` | Map scrolling flags to lifecycle invocation |
| `capture/lifecycle.zig` | Add scrolling capture path (preflight -> session -> post-capture) |
| `backends/capture_execution_plan.zig` | Add `scrolling` operation |
| `compositor/runtime.zig` | Expose `zwlr_virtual_pointer_manager_v1` availability check |
| `compositor/focused_output.zig` | Used for output-scoped capture |
| `overlay/selection_session.zig` | Reused for region selection |
| `pipeline/post_capture.zig` | Same save/copy/preview pipeline |
| `config.toml` | Add `[capture.scrolling]` section |

### 9.3 Wayland connection ownership

The scrolling capture loop needs a long-lived Wayland connection for
repeated `capture_output_region` calls and virtual pointer events. This is
different from Shaula's current one-shot capture model (spawn grim, read
output).

Options:
- **(A) Native Zig Wayland client**: Use `wayland-client` bindings via Zig's
  C interop to directly call `zwlr_screencopy_manager_v1` and
  `zwlr_virtual_pointer_manager_v1`. This is what scrollshot does in Rust.
- **(B) Helper binary**: A small standalone Wayland client binary (written in
  C or compiled alongside Shaula) that accepts region coordinates over
  stdio, runs the capture loop, and writes frame PNGs to a temp directory.
  Shaula's Zig core orchestrates and stitches.

**Decision**: Option (B) — a helper binary. This matches Shaula's existing
architecture pattern (overlay helper, preview helper) and keeps the
Wayland protocol code isolated. The helper binary (`shaula-scroll-helper`)
communicates over stdio JSON:

```
Input:  { "region": {x,y,w,h}, "output_global": N, "scroll_delay": 200, "scroll_ticks": 2, "max_frames": 200 }
Output: frame paths written to $XDG_RUNTIME_DIR/shaula/scroll-<ts>/frame_N.png
        final JSON: { "frames": N, "paths": [...] }
```

This keeps the Wayland C code in a separate compilation unit (like the
existing overlay helper) and lets the Zig core own stitching, post-capture,
and the product contract.

### 9.4 Stitching in Zig

The stitching algorithms (row-vote and col-sample) are pure pixel math with
no Wayland dependency. They should be implemented directly in Zig in
`scrolling_stitch.zig`, operating on `image.RgbaImage`-equivalent buffers
loaded from the helper's frame PNGs. No OpenCV dependency.

## 10. Performance Budget

| Metric | Budget | Rationale |
|---|---|---|
| Single frame capture | < 50ms | `zwlr_screencopy` is fast; SHM read is the bottleneck |
| Scroll settle per frame | scroll_delay (200ms default) | Wait for re-render |
| Stitch per frame pair (row-vote) | < 100ms for 1920x1080 | O(h² * w/4) is manageable at 1080p |
| Stitch per frame pair (col-sample) | < 20ms for 1920x1080 | O(9 * h) is very fast |
| Total capture+stitch for 50 frames | < 15s auto, < 25s manual | Mostly I/O bound |
| Peak memory | < 2 * max_height * width * 4 | Two frame buffers + growing output |

## 11. Implementation Phases

### Phase 1: Manual scroll + row-vote stitch (v1 safe path)

- Region selection (reuse existing overlay)
- Manual scroll mode only (no virtual pointer yet)
- Periodic frame capture via grim (simpler than native screencopy)
- Row-vote stitching in Zig
- Post-capture pipeline (save/copy/preview)
- CLI: `shaula capture scrolling --scroll-mode manual`

**Why manual first**: No virtual pointer protocol dependency. User scrolls
at their own pace. Simpler to implement and test. Validates the stitching
algorithm with real content before adding auto-scroll complexity.

### Phase 2: Auto-scroll + native capture (v2 magic path)

- `shaula-scroll-helper` Wayland client binary
- `zwlr_virtual_pointer_manager_v1` for auto-scroll
- `zwlr_screencopy_manager_v1` for frame capture (no grim fork)
- End-of-scroll detection (consecutive identical frames)
- Initial frame stabilization
- CLI: `shaula capture scrolling` (auto by default)
- Fallback to manual when virtual pointer unavailable

### Phase 3: Polish

- Col-sample stitching algorithm
- Side/bottom edge trimming in overlap detection
- Real-time preview during capture (layer-shell thumbnail, like wayscrollshot)
- Fixed-header detection and masking
- Scroll-direction up (reverse scrolling)
- Max-height enforcement with partial-result warnings
- `shaula doctor` integration (check virtual pointer protocol availability)

## 12. Testing Strategy

- **Unit tests**: Stitching algorithms with synthetic striped/diagonal images
  (matching scrollshot's approach: unique-per-row color patterns for
  deterministic scroll detection).
- **Integration tests**: Capture loop with a known-scrollable test window
  (e.g., a long GTK text view). Compare stitched output against expected
  content.
- **Edge-case tests**: Identical frames, single-frame capture, zero-height
  overlap, very-wide and very-narrow regions, blank/whitespace pages.
- **No regression tests**: Per AGENTS.md, this app doesn't have users yet.
- **Verification**: `./dev check && git diff --check` after every change.
