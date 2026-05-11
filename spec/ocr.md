# Shaula OCR Specification

## Summary

Add OCR (Optical Character Recognition) to Shaula so the user can select a screen region and instantly get recognized text copied to the Wayland clipboard. All processing is local, offline, and deterministic — no cloud, no telemetry.

## Research Findings

### How existing screenshotters implement OCR

| Tool | Engine | Integration | Key observations |
|------|--------|-------------|------------------|
| **Shottr** (macOS) | Apple Vision / native macOS OCR | Built-in, deeply integrated | Fully local, also decodes QR codes. Hotkey triggers area-select → OCR → clipboard. Strips double spaces, offers linebreak-removal toggle. |
| **NormCap** (cross-platform) | Tesseract 5+ via `tesserocr` Python bindings | Bundled in Flatpak/AppImage | Uses PySide6 (Qt) for capture overlay. Supports `wl-copy`/`xclip`/Qt-clipboard for Wayland. Heuristic "magics" post-process text (URLs, emails, single/multi-line). QR/barcode via zxing-cpp. |
| **Frog** (GNOME) | Tesseract via Python `pytesseract` | Calls Tesseract CLI | GNOME/libadwaita UI. Screenshot → Tesseract → clipboard. Also supports QR decode. |
| **Flameshot** (Linux) | Tesseract C API (attempted PRs) | Never merged — deemed out-of-scope | Three separate PRs (#1239, #2843, #3074, #4228) tried: C++ Tesseract link, local OCR server, PATH-based CLI call. Devs rejected: "OCR is not screenshot annotation" → must be plugin. |
| **PowerToys Text Extractor** (Windows) | `Windows.Media.Ocr` (system OCR) | Native WinRT API | Scales image to 1.5× before OCR. Language-aware post-processing (CJK space handling). Single hotkey → region select → clipboard. |
| **Unix pipeline** (grim/slurp + tesseract) | Tesseract CLI | Pipe-based | `grim -g "$(slurp)" - \| tesseract stdin stdout \| wl-copy`. Works but no UI feedback, no preprocessing, fixed language. |

### Key architectural patterns

1. **Area-select → OCR → clipboard** is the universal UX. One hotkey, select region, text lands in clipboard.
2. **Tesseract C API** is the only viable local OCR engine for Linux/Wayland. It has a stable C ABI (`capi.h`) with opaque handles — ideal for Zig `@cImport`.
3. **Image preprocessing** is critical for screenshot OCR accuracy: screenshots are typically 96–144 DPI (Tesseract wants 300+), small text needs upscaling, dark-on-light vs light-on-dark needs inversion handling.
4. **Page Segmentation Mode (PSM)** selection matters: screenshots are rarely "pages of text". PSM 6 (single block), PSM 7 (single line), PSM 11 (sparse text) are the useful modes.
5. **Wayland clipboard quirks**: clipboard access requires an active surface in some compositors; `wl-copy` is the standard; `wl-copy --type text/plain` for text. NormCap discovered clipboard reliability issues on Wayland and added a Qt-based clipboard handler as fallback.
6. **QR/barcode as bonus**: Shottr and NormCap both bundle QR reading alongside OCR. Can be a future addition.

## Design Decisions

### OCR Engine: Tesseract 5+ via C API

**Why Tesseract C API over CLI subprocess:**

| Approach | Pros | Cons |
|----------|------|------|
| **C API (`@cImport`)** | Zero subprocess overhead; in-process image passing (no temp file or pipe); access to word-level bounding boxes and confidence scores; PSM/language control per-call; lower latency (~50–200ms typical for screen regions) | Build-time dependency on `libtesseract` + `libleptonica`; must manage Tesseract lifecycle (init/end) correctly |
| **CLI subprocess** | Simpler build; always uses system Tesseract; no ABI coupling | Temp file or pipe overhead; no per-word metadata; slower (~200–500ms for same region); harder to control PSM/language per-call; harder to capture confidence for UI feedback |

**Decision: C API.** Shaula already links native C libraries (GTK, cairo, pango). Adding `libtesseract` + `libleptonica` follows the existing pattern. The C API gives per-word bounding boxes (needed for future interactive word selection) and avoids temp-file latency on the Wayland hot path.

### Tesseract Lifecycle

Tesseract initialization is expensive (~100–300ms). Recognition on a screen region is fast (~50–200ms). The lifecycle must balance startup cost against memory usage:

**Decision: Lazy singleton with configurable TTL.**

- First OCR call initializes `TessBaseAPI` and keeps it alive.
- A configurable idle TTL (default: 30s) ends the instance, releasing ~50–100MB of model memory.
- Next OCR call re-initializes. This avoids holding Tesseract memory between infrequent OCR sessions.
- Language change re-initializes (language switch requires `Init` again).

### Integration Point: Two Modes

Shaula has two distinct surfaces where OCR makes sense:

#### 1. Capture-to-clipboard OCR (primary)

Fast path: user presses OCR hotkey → overlay appears → select region → text copied to clipboard → notification with word count.

Flow:
```
Niri keybind → shaula ocr area --json
  → overlay selection session (reuse existing overlay/selection_session.zig)
  → capture the selected region to a temp PNG
  → Tesseract C API: Init → SetImage(bytes) → Recognize → GetUTF8Text
  → wl-copy --type text/plain (reuse clipboard/service.zig pattern)
  → notify-send with word count preview
  → emit JSON result
```

This mirrors the existing `capture area` pipeline but adds OCR as the output transformation instead of image save/copy.

#### 2. Preview toolbar OCR (secondary)

In the post-capture preview window, an OCR toolbar button extracts text from the visible image or a selected region.

Flow:
```
Preview toolbar: shaula-ocr-symbolic button
  → if region selected: OCR that crop
  → else: OCR the full preview image
  → Tesseract C API (same module)
  → copy text to clipboard via wl-copy
  → toast notification with word count
```

This reuses the preview's existing selection and annotation infrastructure.

### Image Preprocessing Pipeline

Screenshots need preprocessing that document scans do not:

```
captured PNG
  → DPI detection (check PNG pHYs chunk, fallback to compositor-reported scale × 96)
  → if effective DPI < 300: upscale by ceil(300 / effectiveDPI)
  → if dark-on-light / light-on-dark: Tesseract LSTM auto-detects (invert_threshold=0.7)
  → add 10px white border (prevents edge clipping artifacts)
  → SetSourceResolution(300)
  → SetPageSegMode(PSM_AUTO or PSM_SPARSE_TEXT)
```

No grayscale conversion needed — Tesseract/Leptonica handle binarization internally. The upscaling + border + PSM selection covers the 90% case for screen text.

### Language Handling

- Default: system locale language (via `LC_CTYPE` / `LANG` → extract ISO 639-1 code).
- Config: `[ocr] language = "eng+spa"` in `config.toml`.
- CLI: `shaula ocr area --lang eng+deu --json`.
- Tesseract tessdata discovery: `TESSDATA_PREFIX` env var → `/usr/share/tessdata` → `/usr/local/share/tessdata` → fail with `ERR_OCR_TESSDATA_NOT_FOUND`.

### Error Contract

All OCR errors use the existing `ERR_*` deterministic pattern:

| Code | Meaning |
|------|---------|
| `ERR_OCR_UNAVAILABLE` | Tesseract library not installed or not loadable |
| `ERR_OCR_TESSDATA_NOT_FOUND` | No tessdata directory or language pack found |
| `ERR_OCR_INIT_FAILED` | `TessBaseAPIInit3` returned non-zero |
| `ERR_OCR_RECOGNIZE_FAILED` | `TessBaseAPIRecognize` returned non-zero |
| `ERR_OCR_NO_TEXT` | Recognition succeeded but output is empty or whitespace-only |
| `ERR_OCR_CLIPBOARD_FAILED` | `wl-copy` text copy failed |
| `ERR_OCR_CAPTURE_FAILED` | Region capture failed before OCR could run |

### CLI Contract

New `ocr` command family:

```text
shaula ocr area --json [--lang <codes>] [--psm <mode>] [--copy|--no-copy]
shaula ocr file <path> --json [--lang <codes>] [--psm <mode>] [--copy|--no-copy]
```

- `ocr area`: launch overlay → select region → OCR → clipboard.
- `ocr file`: OCR an existing image file (no overlay).
- `--lang`: Tesseract language codes (default: config or system locale).
- `--psm`: Tesseract PSM override (default: auto-selected from region size heuristic).
- `--copy` (default): copy recognized text to clipboard.
- `--no-copy`: skip clipboard, only return text in JSON.

### JSON Output Contract

```json
{
  "ok": true,
  "contract_version": "1.0.0",
  "command": "ocr area",
  "timestamp": "2026-05-11T10:30:00Z",
  "result": {
    "text": "Hello World",
    "word_count": 2,
    "char_count": 11,
    "language": "eng",
    "psm_used": 6,
    "confidence": 95,
    "clipboard": true,
    "regions": [
      { "word": "Hello", "x": 10, "y": 20, "w": 50, "h": 18, "confidence": 97 },
      { "word": "World", "x": 70, "y": 20, "w": 45, "h": 18, "confidence": 93 }
    ]
  },
  "warnings": []
}
```

Error:
```json
{
  "ok": false,
  "contract_version": "1.0.0",
  "command": "ocr area",
  "timestamp": "2026-05-11T10:30:00Z",
  "error": {
    "code": "ERR_OCR_TESSDATA_NOT_FOUND",
    "message": "no tessdata found for language 'eng' in /usr/share/tessdata",
    "retryable": true
  },
  "warnings": []
}
```

The `regions` array with per-word bounding boxes and confidence is optional and enables future interactive word selection in the preview UI.

## Architecture

### New Modules

```
src/ocr/
  engine.zig        — Tesseract C API wrapper (lifecycle, init, recognize, teardown)
  preprocess.zig    — DPI detection, upscaling, border padding (uses existing pixbuf/cairo)
  language.zig      — tessdata path discovery, system locale → Tesseract lang code mapping
  service.zig       — high-level OCR orchestration: capture → preprocess → recognize → clipboard
  command.zig       — CLI `ocr area` / `ocr file` dispatcher
  types.zig         — OcrResult, OcrWord, OcrRegion, error codes
```

### Tesseract C API Binding Pattern

```zig
// src/ocr/engine.zig
const c = @cImport({
    @cInclude("tesseract/capi.h");
});

pub const TessHandle = *opaque {};

pub const Engine = struct {
    handle: ?*c.TessBaseAPI = null,
    language: []const u8,
    initialized: bool = false,

    pub fn init(self: *Engine, datapath: ?[*:0]const u8, lang: [*:0]const u8) !void {
        self.handle = c.TessBaseAPICreate();
        const rc = c.TessBaseAPIInit3(self.handle, datapath, lang);
        if (rc != 0) {
            c.TessBaseAPIDelete(self.handle);
            self.handle = null;
            return error.TessInitFailed;
        }
        self.initialized = true;
    }

    pub fn recognizeFromBytes(
        self: *Engine,
        data: [*]const u8,
        width: i32,
        height: i32,
        bpp: i32,
        bytes_per_line: i32,
    ) ![]u8 {
        c.TessBaseAPISetImage(self.handle, data, width, height, bpp, bytes_per_line);
        const rc = c.TessBaseAPIRecognize(self.handle, null);
        if (rc != 0) return error.RecognizeFailed;
        const text = c.TessBaseAPIGetUTF8Text(self.handle);
        if (text == null) return error.NoText;
        defer c.TessDeleteText(text);
        return std.mem.sliceTo(text, 0);
    }

    pub fn deinit(self: *Engine) void {
        if (self.handle) |h| {
            if (self.initialized) c.TessBaseAPIEnd(h);
            c.TessBaseAPIDelete(h);
            self.handle = null;
            self.initialized = false;
        }
    }
};
```

### Build Integration

In `build.zig`:

```zig
// Link tesseract and leptonica for OCR
exe.linkSystemLibrary("tesseract");
exe.linkSystemLibrary("lept");
exe.linkLibC();
```

At runtime, if `libtesseract.so` is not installed, the `ocr` command returns `ERR_OCR_UNAVAILABLE` instead of failing to launch. This requires a dlopen-based lazy binding strategy so the main `shaula` binary starts even without Tesseract installed.

**Alternative (recommended for Shaula's conservative approach):** Build `shaula` with Tesseract linked normally, but the `ocr` command family is the only entry point that calls into it. If the shared lib is missing at runtime, the dynamic linker error is caught and mapped to `ERR_OCR_UNAVAILABLE`. In practice, distributions package `libtesseract` and `tesseract-ocr-eng` together, so this is a non-issue for installed users.

### Clipboard Integration

Reuse the existing `clipboard/service.zig` pattern but add text clipboard support:

```zig
pub fn copyText(io: std.Io, text: []const u8) !bool {
    const term = process_exec.runWithPipeInput(io, &.{ "wl-copy", "--type", "text/plain" }, text) catch return false;
    return switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
}
```

This mirrors `publishWaylandImage` but for `text/plain` instead of `image/png`.

### PSM Selection Heuristic

For screenshot OCR, automatic PSM selection improves results:

| Region characteristics | PSM | Reason |
|------------------------|-----|--------|
| Small area, few words | 7 (single line) or 8 (single word) | Avoids over-segmentation |
| Medium area, multiple lines | 6 (single block) | Most UI text is a uniform block |
| Large area, mixed content | 3 (auto) or 11 (sparse text) | Desktop screenshots are sparse |
| Single character/digit | 10 (single char) | Rare, but useful for color picker hex |

Heuristic: if `region_area < 50×30 px` → PSM 7; if `< 400×200` → PSM 6; else → PSM 11. Overridable via `--psm`.

### Text Post-Processing

Following Shottr and NormCap patterns:

1. **Strip trailing whitespace per line** — Tesseract often adds trailing spaces.
2. **Collapse double spaces** — Shottr specifically prevents accidental double spaces.
3. **Optional linebreak removal** — configurable via `[ocr] remove_linebreaks = true` or a toggle in the preview OCR toast. Joins lines with a single space, preserves paragraph breaks (double newlines).
4. **Trim leading/trailing blank lines** — common Tesseract artifact.

No heuristic "magics" (NormCap's URL/email detection) in v1. These can be future enhancements.

## Doctor Integration

`shaula doctor` should report OCR readiness:

```
OCR:
  tesseract: 5.3.0 (/usr/lib/libtesseract.so)
  tessdata:  /usr/share/tessdata
  languages: eng, spa, deu
  status:    ready
```

Or:
```
OCR:
  tesseract: not found
  tessdata:  —
  languages: —
  status:    unavailable (install tesseract-ocr and a language pack)
```

## Capabilities Integration

`shaula capabilities list --json` gains an `ocr` capability:

```json
{
  "capabilities": [
    { "name": "ocr", "available": true, "engine": "tesseract", "version": "5.3.0", "languages": ["eng", "spa"] }
  ]
}
```

## Preview Toolbar Addition

New toolbar button: `shaula-ocr-symbolic` (text extraction icon).

- Available when Tesseract is present (checked at preview startup).
- Clicking it:
  - If a region selection exists: OCR that region.
  - Else: OCR the full visible image.
- Shows a toast with: word count, first ~50 chars preview, "Copied" confirmation.
- Respects the existing annotation system: OCR reads from the composited document (base image + annotations rendered), matching what the user sees.

## Niri Keybind

```kdl
// In generated niri-shaula.kdl
binds {
    Mod+Shift+O { spawn "shaula" "ocr" "area" "--json"; }
}
```

## Implementation Phases

### Phase 1: Core OCR Pipeline

- `src/ocr/engine.zig`: Tesseract C API wrapper with lazy init, TTL, and proper cleanup.
- `src/ocr/preprocess.zig`: DPI detection, upscaling, border padding.
- `src/ocr/language.zig`: tessdata path discovery, locale mapping.
- `src/ocr/service.zig`: capture → preprocess → recognize → clipboard orchestration.
- `src/ocr/types.zig`: error codes, result types.
- `src/ocr/command.zig`: `shaula ocr area --json` and `shaula ocr file <path> --json`.
- `build.zig`: link `tesseract` and `lept`.
- `clipboard/service.zig`: add `copyText` function.

### Phase 2: Doctor and Capabilities

- `doctor/diagnostics.zig`: Tesseract/tessdata/language discovery.
- `capabilities/probe.zig`: `ocr` capability reporting.
- `preflight/probe.zig`: OCR availability check for area-ocr path.

### Phase 3: Preview Integration

- `shaula-ocr-symbolic` SVG icon asset.
- `preview_commands.h`: `SHAULA_PREVIEW_COMMAND_OCR`.
- Preview C code: OCR button, region-or-full detection, toast notification.
- `preview_clipboard.zig`: text clipboard copy from preview.

### Phase 4: Post-processing and Polish

- Text cleanup (double-space collapse, linebreak toggle).
- Config `[ocr]` section: `language`, `remove_linebreaks`, `idle_ttl_seconds`.
- Per-word bounding boxes in JSON (for future interactive word selection).
- QR code detection (bonus, via zbar or zxing-cpp — separate spec).

## Dependencies

Runtime:
- `libtesseract` (≥5.0) — OCR engine
- `libleptonica` — image I/O for Tesseract (transitive dependency)
- `tesseract-ocr-eng` (or user's preferred language pack) — trained model data
- `wl-copy` (already required) — Wayland clipboard

Build:
- `libtesseract-dev` / `tesseract-ocr-devel` — headers for `@cImport`
- `libleptonica-dev` / `leptonica-devel` — headers (transitive via tesseract)

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Tesseract not installed | `ERR_OCR_UNAVAILABLE` with actionable message; `shaula doctor` reports missing deps; OCR command is opt-in, never blocks other features |
| Tesseract init slow (~200ms) | Lazy singleton with TTL; first-call penalty is acceptable for an infrequent tool; subsequent calls are fast |
| Low accuracy on small/dark text | Preprocessing pipeline (upscaling, border, DPI hint); PSM heuristic; user can override PSM/language |
| Memory usage (~50–100MB per language) | TTL-based teardown; single-language default; warn in doctor if many languages loaded |
| ABI break on Tesseract major version bump | C API (`capi.h`) has been stable since Tesseract 3.02; pin to ≥5.0 at build time |
| Wayland clipboard race (no active surface) | Shaula's overlay creates a layer-shell surface before OCR runs; `wl-copy` works from that context; if it fails, `ERR_OCR_CLIPBOARD_FAILED` is reported and text is still in JSON |
