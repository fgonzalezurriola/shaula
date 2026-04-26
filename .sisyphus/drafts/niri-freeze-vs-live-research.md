# Niri Screenshot Selection Behavior: Freeze vs Live, and Shaula Two-Mode Design

## 1) What Niri does today (with references)

### Finding summary
Niri’s built-in screenshot UI **freezes the scene at screenshot-UI open time** and then performs selection over that frozen texture set. It does not keep sampling live scene frames while the user drags selection.

### Evidence from implementation

1. **Screenshot UI entrypoint captures output textures once before UI opens**
   - `third_party/niri/src/niri.rs:1949` (`open_screenshot_ui`)
   - `third_party/niri/src/niri.rs:1962` updates render elements once.
   - `third_party/niri/src/niri.rs:1964-1967` calls `capture_screenshots(renderer).collect()`.
   - `third_party/niri/src/niri.rs:1982-1986` passes captured screenshots into `screenshot_ui.open(...)`.

2. **Captured screenshots are stored in ScreenshotUi open state**
   - `third_party/niri/src/ui/screenshot_ui.rs:138` (`ScreenshotUi::open`)
   - `third_party/niri/src/ui/screenshot_ui.rs:142` receives `HashMap<Output, [OutputScreenshot; 3]>`.
   - `third_party/niri/src/ui/screenshot_ui.rs:186-225` stores these in `output_data`.
   - `third_party/niri/src/ui/screenshot_ui.rs:232-241` transitions to `Self::Open { ... output_data ... }`.

3. **Render path for open screenshot UI uses stored screenshot buffers, not fresh scene render**
   - `third_party/niri/src/niri.rs:4261-4269`: if screenshot UI is open, `self.screenshot_ui.render_output(...)` is used and normal scene path returns early.
   - `third_party/niri/src/ui/screenshot_ui.rs:622` (`render_output`)
   - `third_party/niri/src/ui/screenshot_ui.rs:681-695` draws `output_data.screenshot[index]` (pre-captured buffers).

4. **Selection changes only update overlays/rect geometry, not source frame**
   - `third_party/niri/src/ui/screenshot_ui.rs:549` (`update_buffers`)
   - `third_party/niri/src/ui/screenshot_ui.rs:568-618` updates border/dim buffers and locations.
   - `third_party/niri/src/ui/screenshot_ui.rs:801-845` pointer motion mutates selection geometry and calls `update_buffers()`.

5. **Final capture is a crop/composite from frozen screenshot texture**
   - `third_party/niri/src/ui/screenshot_ui.rs:697` (`capture`)
   - `third_party/niri/src/ui/screenshot_ui.rs:713-717` uses selected output and `data.screenshot[0]` texture.
   - `third_party/niri/src/ui/screenshot_ui.rs:718-765` crops and optionally composites pointer from stored textures.

6. **Niri has a separate explicit “freeze screen” primitive used elsewhere (screen transition)**
   - `third_party/niri/src/niri.rs:6226` (`do_screen_transition`)
   - `third_party/niri/src/niri.rs:6231-6287` captures textures for output/screencast/screencapture targets.
   - `third_party/niri/src/niri.rs:6293-6300` installs `ScreenTransition` from those textures.
   - `third_party/niri/src/niri.rs:6302-6303` explicit freeze-oriented comment.

### Interpretation
Niri native screenshot-selection UX is architected as a **single-shot snapshot then interactive selection overlay**. This corresponds to “freeze-on-select” semantics from the user perspective.

---

## 2) Feasibility implications for Shaula Wayland/Niri path

1. **Both behaviors are feasible on Wayland/Niri**
   - `live-preview`: keep current behavior (selection over continuously changing scene).
   - `freeze-on-select`: take one snapshot at selection start and render selection from that static source.

2. **Freeze mode aligns with compositor-native behavior**
   Niri already uses this model for its own screenshot UI, so users on Niri are likely to find freeze mode intuitive and stable.

3. **Contract and taxonomy stability can be preserved**
   Shaula can add mode selection as a policy/config input while keeping existing JSON schema stable and mapping failures to deterministic `ERR_*` codes.

4. **Main engineering delta is source-frame ownership**
   - Live mode source = real-time frame feed.
   - Freeze mode source = immutable frame buffer acquired exactly once at selection start.
   The rest of selection UX (rect manipulation, keyboard shortcuts, confirm/cancel) can be shared.

---

## 3) Proposed abstraction and mode-switch design for Shaula

### Proposed enum
```zig
pub const SelectionPreviewMode = enum {
    live_preview,
    freeze_on_select,
};
```

### Runtime policy contract
- Default: `live_preview` (explicitly preserves current Shaula behavior).
- New optional policy key:
  - CLI/config-internal: `selection_preview_mode`
  - accepted values: `live-preview`, `freeze-on-select`
- Unknown value handling: deterministic config/policy error with existing taxonomy style (`ERR_*`) and stable JSON error envelope.

### Pipeline abstraction
Introduce a narrow frame-source interface used by overlay/selection renderer:

```zig
const FrameSource = union(enum) {
    live: LiveFrameProvider,
    frozen: FrozenFrame,
};

pub fn beginSelection(mode: SelectionPreviewMode, ctx: *SelectionContext) !FrameSource {
    return switch (mode) {
        .live_preview => .{ .live = try LiveFrameProvider.init(ctx) },
        .freeze_on_select => .{ .frozen = try captureFrozenFrame(ctx) },
    };
}
```

### Behavioral contracts
- `live-preview`
  - Source frame may change between pointer moves.
  - Selected pixels reflect scene at confirmation instant (current behavior).
- `freeze-on-select`
  - Source frame is captured exactly once at selection start.
  - Selected pixels MUST come from that same frozen buffer regardless of scene updates.

### Deterministic error contracts (pattern)
Keep explicit deterministic outputs consistent with current taxonomy style:
- `ERR_SELECTION_SOURCE_UNAVAILABLE` (if frame source cannot initialize)
- `ERR_SELECTION_SOURCE_TIMEOUT` (if freeze capture times out)
- `ERR_SELECTION_PROTOCOL_INVALID` (if helper/IPC payload malformed)

(Use existing Shaula taxonomy codes/names where already defined; above names are mode-related contract intents.)

---

## 4) Suggested migration steps (incremental, low-risk)

1. **Step A: Policy plumbing only**
   - Add mode parse/validation and carry through selection request.
   - Keep runtime behavior unchanged (`live-preview` only active).

2. **Step B: FrameSource abstraction with live implementation**
   - Refactor current selection rendering to consume `FrameSource`.
   - Verify no behavior change in default mode.

3. **Step C: Add freeze capture path**
   - On selection start in `freeze-on-select`, acquire single immutable frame.
   - Render overlays from frozen buffer until confirm/cancel.

4. **Step D: Confirm-path and output parity checks**
   - Ensure JSON output, dimensions, mime/path, and warning fields remain schema-stable.

5. **Step E: Guardrails and telemetry**
   - Add deterministic warning token when freeze mode falls back (if fallback policy allows).
   - Keep no-op on non-Niri paths unless feature explicitly enabled.

---

## 5) QA strategy for both modes

### Functional matrix
1. **Live mode**
   - Use animated content under selection area.
   - Assert preview reflects frame changes while dragging.
2. **Freeze mode**
   - Start selection over animated content.
   - Assert preview remains static while animation continues in background.
   - Confirm resulting crop matches start-frame pixels, not end-frame pixels.

### Contract QA
- For both modes, assert same JSON key-set and deterministic field semantics.
- Assert deterministic `ERR_*` mapping for:
  - source init failure,
  - timeout,
  - invalid helper payload.

### Regression QA
- Existing capture-area tests must pass unchanged in default mode.
- Add mode-specific tests with explicit mode fixture.
- Add one e2e smoke case in Niri environment for `freeze-on-select`.

---

## Recommendation
Implement the two-mode model via a shared selection engine plus swappable frame-source policy. This keeps current Shaula UX unchanged by default (`live-preview`) while enabling Niri-aligned `freeze-on-select` behavior with deterministic contracts and low migration risk.
