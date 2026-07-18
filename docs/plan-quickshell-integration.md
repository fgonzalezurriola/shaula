# QuickShell Integration Plan

> Status: **Draft** — Planning phase, not yet approved for implementation  
> Date: 2026-05-29  
> Owner: Shaula project  

---

## 1. Executive Summary

Expand Shaula's shell integration beyond the Noctalia-specific plugin to a **general QuickShell integration layer**. This enables any QuickShell-based shell (Noctalia, custom qs configs, community bars) to discover, invoke, and respond to Shaula capture workflows through a standardized IPC + plugin architecture, while preserving the existing Noctalia integration as the reference implementation.

**Why now?**

- Noctalia is a QuickShell shell; the existing plugin already uses `Quickshell.execDetached()` to call `shaula capture ...`.
- Other QuickShell configs (niri bars, Hyprland panels) could host a Shaula widget with minimal changes if we extract the integration layer.
- QuickShell's `IpcHandler` system provides a bidirectional IPC channel that the current Noctalia plugin doesn't use — enabling real-time state sync (capture in progress, panel hide handshake) instead of fire-and-forget CLI calls.
- The `command.c` panel-hide handshake already exists but is file-token based; IPC-based handshake would be cleaner and more reliable.

---

## 2. Current State Analysis

### 2.1 Existing Noctalia Integration

| Component | Path | Role |
|-----------|------|------|
| `manifest.json` | `integrations/noctalia/shaula/manifest.json` | Plugin identity (id `shaula`, v0.1.6, entryPoint `BarWidget.qml`) |
| `BarWidget.qml` | `integrations/noctalia/shaula/BarWidget.qml` | QML bar widget: icon button → context menu → `Quickshell.execDetached(["sh","-lc","shaula capture ..."])` |
| `noctalia-action-adapter.sh` | `integrations/noctalia/noctalia-action-adapter.sh` | Bash bridge: builds JSON action menu (8 actions), resolves action→shaula-argv, executes CLI, handles open targets |
| `noctalia-plugin-poc.sh` | `integrations/noctalia/noctalia-plugin-poc.sh` | Outer CLI wrapper parsing `--menu`/`--action`/`--dry-run` |
| `setup/command.c` | `src/main.c and scripts/install.sh` | Installer: copies plugin files to `~/.config/noctalia/plugins/shaula/`, edits `plugins.json` (v2) + `settings.json` |
| `doctor/diagnostics.c` | `src/main.c` | Health checks: Noctalia paths, plugin dir, enabled state |
| `command.c` | `src/capture/command.c` | Panel-hide handshake via `SHAULA_PANEL_HIDDEN_TOKEN_FILE` file token polling |

**Limitations of current approach:**

1. **Fire-and-forget**: `execDetached` calls `shaula` CLI and forgets — no feedback on capture success/failure/progress.
2. **No bidirectional state**: The bar widget doesn't know if a capture is already in progress (`ERR_CAPTURE_IN_PROGRESS`).
3. **File-token handshake**: Panel-hide coordination uses filesystem polling (`panelHiddenTokenObserved`), which is latency-bounded and fragile.
4. **Noctalia-only**: The plugin imports Noctalia-specific QML modules (`qs.Commons`, `qs.Modules.Bar.Extras`, `qs.Services.UI`, `qs.Widgets`) and uses Noctalia's `NIconButton`, `NPopupContextMenu`, `PanelService`, `BarService`, `Style`, `Color` — making it non-portable to other QuickShell shells.
5. **Bash adapter indirection**: The action-adapter shell scripts are a Bash→Python→JSON bridge that duplicates the QML action mapping.

### 2.2 QuickShell Architecture (Reference)

QuickShell is a C++/QML desktop shell framework for Wayland and X11.

**Key QML types for this integration:**

| Type | Module | Purpose |
|------|--------|---------|
| `Quickshell` | `Quickshell` (core) | Singleton: `execDetached()`, `screens`, `shellDir`, `dataDir`, `stateDir` |
| `PanelWindow` | `Quickshell.Wayland` | Layer-shell panel (bar, dock) |
| `PopupWindow` | `Quickshell` | Anchored popup menus |
| `Variants` | `Quickshell` (core) | Per-screen repeater for windows |
| `IpcHandler` | `Quickshell.Io` | Register named IPC target with callable functions/signals/properties |
| `Process` | `Quickshell.Io` | Subprocess lifecycle management (`exec()`, `startDetached()`) |
| `Singleton` | `Quickshell` (core) | Base for QML singletons that survive reload |
| `LazyLoader` | `Quickshell` (core) | Async component loading |

**IpcHandler protocol** (the key unlock):

```
┌──────────────────────┐     QS IPC Socket      ┌──────────────────────┐
│   External Process   │ ──── `qs ipc call` ──── │  QuickShell Instance │
│   (shaula CLI)       │ ◄─── `qs ipc listen` ── │  (IpcHandler QML)    │
│                      │ ──── `qs ipc prop get`─ │                      │
└──────────────────────┘                         └──────────────────────┘
```

- Register: `IpcHandler { target: "shaula"; function capture(mode: string): void { ... } }`
- Call: `qs ipc call shaula capture area`
- Listen: `qs ipc listen shaula captureStarted` / `qs ipc wait shaula captureComplete`
- Read: `qs ipc prop get shaula captureInProgress`

**Noctalia's plugin system** (how the existing widget loads):

1. `PluginRegistry` scans `~/.config/noctalia/plugins/*/manifest.json`
2. `PluginService` loads enabled plugins via `Qt.createComponent()`, instantiates `entryPoints.barWidget`
3. `BarWidgetLoader` injects `pluginApi` property, registers widget as `plugin:<id>` in the bar
4. Plugin QML can import Noctalia's `qs.*` modules for native look-and-feel

---

## 3. Target Architecture

### 3.1 Integration Layers

```
┌─────────────────────────────────────────────────────────────┐
│                   QuickShell Shell                          │
│  (Noctalia, custom qs config, any QuickShell-based bar)     │
│                                                             │
│  ┌────────────────────┐  ┌──────────────────────────────┐  │
│  │ Shaula Bar Widget  │  │  Shaula IpcHandler Service   │  │
│  │ (UI: menu, icon,   │  │  (bidirectional state:       │  │
│  │  status indicator) │  │   captureState, panelHide,   │  │
│  │                    │  │   captureStarted signal,      │  │
│  │  calls IpcHandler  │  │   captureComplete signal)     │  │
│  └────────┬───────────┘  └──────────┬───────────────────┘  │
│           │                         │                      │
│           │    QS IPC Socket        │                      │
│           │    (qs ipc call/listen) │                      │
└───────────┼─────────────────────────┼──────────────────────┘
            │                         │
            ▼                         ▼
┌───────────────────────────────────────────────────────────┐
│                    Shaula CLI Runtime                       │
│                                                           │
│  `shaula capture <mode> --json`    ← existing CLI path    │
│  `shaula ipc call <target> ...`   ← new IPC path          │
│  `shaula ipc listen <target> ...` ← new IPC path          │
│                                                           │
│  Precondition guard: IPC-based panel hide handshake       │
│  (replaces file-token polling when QS IpcHandler present) │
└───────────────────────────────────────────────────────────┘
```

### 3.2 Three-Tier Plugin Model

| Tier | Scope | QML Dependencies | Distribution |
|------|-------|-----------------|--------------|
| **Core** | Pure QuickShell API only (`Quickshell`, `Quickshell.Io`, `Quickshell.Wayland`) | None | `integrations/quickshell/shaula-core/` |
| **Noctalia** | Extends Core with Noctalia look-and-feel (`qs.Commons`, `qs.Widgets`, `NIconButton`, etc.) | Noctalia shell | `integrations/noctalia/shaula/` (existing, refactored) |
| **Standalone** | Self-contained generic bar widget for any QS config | None | `integrations/quickshell/shaula-standalone/` |

**Core** provides the `ShaulaService.qml` (Singleton + IpcHandler) that any shell can import.  
**Noctalia** wraps Core with Noctalia-native UI components.  
**Standalone** wraps Core with generic QtQuick Controls for shells without a design system.

---

## 4. Detailed Design

### 4.1 ShaulaService.qml (Core IPC Service)

The heart of the integration. A QuickShell `Singleton` that:

1. Registers an `IpcHandler` with target `"shaula"`.
2. Exposes capture state as IPC properties.
3. Emits IPC signals on capture lifecycle events.
4. Manages the panel-hide handshake over IPC.

```qml
// integrations/quickshell/shaula-core/ShaulaService.qml
pragma Singleton
import Quickshell
import Quickshell.Io

Singleton {
    id: service

    // ── State (readable via `qs ipc prop get shaula <prop>`) ──
    property bool captureInProgress: false
    property string lastCaptureMode: ""
    property string lastCaptureResult: ""   // "ok" | "error" | ""
    property string lastCapturePath: ""
    property bool panelHidden: false

    // ── IPC Handler ──
    IpcHandler {
        target: "shaula"
        enabled: true

        // Functions callable via: qs ipc call shaula <function> [args]
        function capture(mode: string): void {
            service.startCapture(mode)
        }

        function panelHide(): void {
            service.panelHidden = true
            service.panelHiddenChanged()  // signal
        }

        function panelShow(): void {
            service.panelHidden = false
        }

        function status(): string {
            return JSON.stringify({
                captureInProgress: service.captureInProgress,
                lastCaptureMode: service.lastCaptureMode,
                lastCaptureResult: service.lastCaptureResult,
                panelHidden: service.panelHidden
            })
        }

        // Signals listable via: qs ipc listen shaula <signal>
        signal captureStarted(string mode)
        signal captureCompleted(string mode, string result, string path)
        signal captureFailed(string mode, string error)
    }

    // ── Capture Execution ──
    function startCapture(mode: string): void {
        if (captureInProgress) return
        captureInProgress = true
        lastCaptureMode = mode
        captureStarted(mode)

        var proc = Qt.createQmlObject('
            import Quickshell.Io
            Process {
                command: ["shaula", "capture", "' + mode + '", "--json"]
                onExited: function(code, status) {
                    service.captureInProgress = false
                    // parse stdout for result
                    service.lastCaptureResult = code === 0 ? "ok" : "error"
                    service.captureCompleted(mode, service.lastCaptureResult, "")
                }
            }
        ', service)
        proc.exec()
    }
}
```

**IPC function catalog:**

| Function | Signature | Description |
|----------|-----------|-------------|
| `capture` | `capture(mode: string)` | Initiate a capture (area, quick, fullscreen, all-screens, window) |
| `panelHide` | `panelHide()` | Acknowledge panel-hide handshake (panel has hidden) |
| `panelShow` | `panelShow()` | Signal panel has reappeared |
| `status` | `status(): string` | Return JSON state snapshot |

**IPC signal catalog:**

| Signal | Signature | Consumers |
|--------|-----------|-----------|
| `captureStarted` | `captureStarted(string mode)` | Other widgets, scripts |
| `captureCompleted` | `captureCompleted(string mode, string result, string path)` | Status indicator, notifications |
| `captureFailed` | `captureFailed(string mode, string error)` | Status indicator, error toast |

**IPC property catalog:**

| Property | Type | Use |
|----------|------|-----|
| `captureInProgress` | `bool` | Shaula CLI precondition guard |
| `panelHidden` | `bool` | Shaula CLI panel-hide handshake |

### 4.2 IPC-Based Panel Hide Handshake

**Current flow** (file-token polling):

```
shaula capture area
  → precondition_guard.enforce()
    → poll SHAULA_PANEL_HIDDEN_TOKEN_FILE every 5ms
    → shell writes token file → guard observes → proceed
  → backend capture
```

**New flow** (IPC-based, fallback to file-token):

```
shaula capture area --panel-handshake=ipc
  → detect QuickShell IPC availability (qs ipc show → target "shaula" exists?)
  → if IPC available:
      → qs ipc call shaula panelHide  (request panel to hide)
      → listen: qs ipc wait shaula panelHiddenChanged
      → guard reads `qs ipc prop get shaula panelHidden` → true → proceed
  → else: fallback to existing file-token flow
  → backend capture
  → qs ipc call shaula panelShow  (request panel to restore)
```

**Advantages:**

- **Deterministic latency**: No polling; the IPC call returns immediately and the property read is synchronous over the socket.
- **Bidirectional**: Shaula can request the panel to hide (instead of just waiting for it), and restore it after capture.
- **Self-documenting**: `qs ipc show` lists all registered targets and their functions/signals/properties.

**Implementation changes in Shaula:**

1. `src/capture/command.c`: Add `ipcPanelHideHandshake()` path that uses `qs ipc call/prop get` instead of file-token polling.
2. Add a separate QuickShell integration probe for `qs ipc show` and a `shaula` target. Do not place child-process execution in the pure `src/compositor/runtime.{c,h}` detector.
3. Fallback chain: IPC handshake → file-token handshake → settle barrier → timeout.

### 4.3 Standalone Bar Widget

A self-contained bar widget that works with any QuickShell config, using only QtQuick Controls 2:

```qml
// integrations/quickshell/shaula-standalone/BarWidget.qml
import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Wayland
import "../shaula-core"   // ShaulaService

PanelWindow {
    id: barWidget

    anchors {
        top: true
        left: true
        right: true
    }
    height: 32
    exclusiveZone: height

    // Minimal bar with just the Shaula icon
    Row {
        anchors.fill: parent
        layoutDirection: Qt.RightToLeft

        ToolButton {
            icon.name: "camera"
            text: "Shaula"
            onClicked: captureMenu.open()

            Menu {
                id: captureMenu
                MenuItem { text: "Quick Capture";   onTriggered: ShaulaService.capture("quick") }
                MenuItem { text: "Capture Area";    onTriggered: ShaulaService.capture("area") }
                MenuItem { text: "Capture Fullscreen"; onTriggered: ShaulaService.capture("fullscreen") }
                MenuItem { text: "Capture All Screens"; onTriggered: ShaulaService.capture("all-screens") }
                MenuSeparator {}
                MenuItem { text: "Settings";        onTriggered: Qt.openUrlExternally("shaula settings") }
            }
        }

        // Status indicator
        Rectangle {
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: ShaulaService.captureInProgress ? "orange" : "transparent"
        }
    }
}
```

### 4.4 Noctalia Plugin Refactor

The existing Noctalia `BarWidget.qml` gets refactored to delegate to `ShaulaService` instead of calling `Quickshell.execDetached` directly:

```qml
// integrations/noctalia/shaula/BarWidget.qml (refactored)
import QtQuick
import Quickshell
import qs.Commons
import qs.Modules.Bar.Extras
import qs.Services.UI
import qs.Widgets
import "../../quickshell/shaula-core"   // ShaulaService

NIconButton {
    id: root
    // ... existing Noctalia properties unchanged ...

    NPopupContextMenu {
        id: contextMenu
        model: [
            { "label": "Quick Capture",       "action": "capture-quick",       "icon": "crop" },
            { "label": "Capture Area",        "action": "capture-area",        "icon": "scan" },
            { "label": "Capture Fullscreen",  "action": "capture-fullscreen",  "icon": "screen-share" },
            { "label": "Capture All Screens", "action": "capture-all-screens", "icon": "layout-dashboard" },
            { "label": "Settings",            "action": "settings",            "icon": "settings" },
            { "label": "Open Screenshots Folder", "action": "open-screenshots-folder", "icon": "folder" },
            { "label": "Report a Bug",        "action": "report-bug",          "icon": "bug" }
        ]
        onTriggered: action => {
            contextMenu.close()
            PanelService.closeContextMenu(screen)
            root.executeAction(action)
        }
    }

    function executeAction(action) {
        switch (action) {
        case "capture-quick":       ShaulaService.capture("quick"); break
        case "capture-area":        ShaulaService.capture("area"); break
        case "capture-fullscreen":  ShaulaService.capture("fullscreen"); break
        case "capture-all-screens": ShaulaService.capture("all-screens"); break
        case "settings":            Quickshell.execDetached(["sh","-lc","shaula settings"]); break
        case "open-screenshots-folder": Quickshell.execDetached(["sh","-lc","shaula directory screenshots --open"]); break
        case "report-bug":          Quickshell.execDetached(["xdg-open","https://github.com/fgonzalezurriola/shaula/issues"]); break
        }
    }

    // Visual: capture-in-progress indicator on the icon
    icon: ShaulaService.captureInProgress ? "loader" : "camera"
}
```

**Key changes from current Noctalia plugin:**

- Capture actions go through `ShaulaService.capture()` instead of `Quickshell.execDetached`.
- `ShaulaService` handles process lifecycle, state tracking, and IPC registration.
- Non-capture actions (settings, folder, bug) remain `execDetached` since they don't need state tracking.
- The Noctalia `manifest.json` remains unchanged — it still declares `entryPoints.barWidget: BarWidget.qml`.

### 4.5 Shaula CLI IPC Subcommand

New CLI subcommand for interacting with QuickShell IPC from scripts and the precondition guard:

```bash
# Discover available QS IPC targets
shaula ipc targets                          # → lists targets (parses `qs ipc show`)

# Call a function on a target
shaula ipc call <target> <function> [args]  # → wraps `qs ipc call`

# Listen for a signal
shaula ipc listen <target> <signal>         # → wraps `qs ipc listen`

# Read a property
shaula ipc prop <target> <property>         # → wraps `qs ipc prop get`

# Check if QS IPC with shaula target is available
shaula ipc available                        # → exit 0/1
```

This subcommand provides:

1. **Abstracted QS IPC access**: Shaula code doesn't need to know `qs` CLI details, just `shaula ipc`.
2. **Fallback compatibility**: `shaula ipc available` returns exit code 0/1, making it safe to use as a precondition gate.
3. **Structured output**: `shaula ipc targets --json` returns machine-readable data.

---

## 5. Implementation Phases

### Phase 0: Preparation (No behavior change)

**Goal**: Restructure the integration directory without breaking anything.

| # | Task | Files | Effort |
|---|------|-------|--------|
| 0.1 | Create `integrations/quickshell/shaula-core/` directory with `qmldir` | New: `integrations/quickshell/shaula-core/qmldir` | 0.5h |
| 0.2 | Write `ShaulaService.qml` (Singleton + IpcHandler) with capture execution | New: `integrations/quickshell/shaula-core/ShaulaService.qml` | 2h |
| 0.3 | Write `ShaulaService` unit tests (QML test runner or manual shell) | New: `integrations/quickshell/tests/` | 2h |
| 0.4 | Create `integrations/quickshell/shaula-standalone/` with generic BarWidget | New: `integrations/quickshell/shaula-standalone/BarWidget.qml` | 1.5h |
| 0.5 | Write README for each integration tier | New: `integrations/quickshell/README.md` | 1h |
| 0.6 | Add ADR: "QuickShell Integration Architecture" | New: `docs/adr/0008-quickshell-integration.md` | 1h |
| | | **Phase total** | **~8h** |

### Phase 1: Noctalia Refactor (Preserve existing behavior)

**Goal**: Refactor Noctalia plugin to use ShaulaService, zero behavior change.

| # | Task | Files | Effort |
|---|------|-------|--------|
| 1.1 | Refactor `BarWidget.qml` to import and use `ShaulaService` for capture actions | Modified: `integrations/noctalia/shaula/BarWidget.qml` | 1h |
| 1.2 | Update Noctalia `manifest.json` if import paths change | Modified: `integrations/noctalia/shaula/manifest.json` | 0.5h |
| 1.3 | Verify Noctalia plugin loads correctly with the service import | Manual test | 1h |
| 1.4 | Update `setup/command.c` to also copy `shaula-core/` files when installing Noctalia plugin | Modified: `src/main.c and scripts/install.sh` | 1.5h |
| 1.5 | Update `doctor/diagnostics.c` to check for `shaula-core/` presence | Modified: `src/main.c` | 0.5h |
| 1.6 | Run existing Noctalia QA scripts, fix regressions | Modified: `scripts/qa/assert-noctalia-*.sh` | 1.5h |
| | | **Phase total** | **~6h** |

### Phase 2: IPC-Based Panel Hide Handshake

**Goal**: Replace file-token handshake with IPC when QuickShell is available.

| # | Task | Files | Effort |
|---|------|-------|--------|
| 2.1 | Add `ipcPanelHideHandshake()` to `command.c` | Modified: `src/capture/command.c` | 3h |
| 2.2 | Add QuickShell IPC availability detection in a separate integration probe | New integration probe; `src/compositor/runtime.{c,h}` remains process-free | 1.5h |
| 2.3 | Implement fallback chain: IPC → file-token → settle barrier → timeout | Modified: `src/capture/command.c` | 2h |
| 2.4 | Add `shaula ipc` subcommand (call, listen, prop, available, targets) | New: `src/ipc/command.c`, `src/ipc/bridge.c` | 4h |
| 2.5 | Write contract tests for IPC handshake paths | New: `scripts/qa/assert-quickshell-ipc-handshake.sh` | 2h |
| 2.6 | Update `ShaulaService.qml` to handle `panelHide`/`panelShow` calls from Shaula | Modified: `integrations/quickshell/shaula-core/ShaulaService.qml` | 1h |
| 2.7 | Update `preflight` and `capabilities` to report IPC availability | Modified: `src/capabilities/`, `src/preflight/` | 1.5h |
| 2.8 | Update `doctor` to report QuickShell IPC readiness | Modified: `src/doctor/` | 0.5h |
| | | **Phase total** | **~15.5h** |

### Phase 3: Capture State Feedback

**Goal**: Enable bidirectional state sync between Shaula and the shell.

| # | Task | Files | Effort |
|---|------|-------|--------|
| 3.1 | ShaulaService: parse capture `--json` output to extract result path, emit `captureCompleted` signal | Modified: `integrations/quickshell/shaula-core/ShaulaService.qml` | 2h |
| 3.2 | Add capture-in-progress visual indicator to both Noctalia and Standalone widgets | Modified: both `BarWidget.qml` files | 1h |
| 3.3 | Add `--panel-handshake=ipc\|file\|auto` CLI flag to `capture/command.c` | Modified: `src/capture/command.c` | 1h |
| 3.4 | Shaula CLI: after capture, call `qs ipc call shaula panelShow` if IPC was used | Modified: `src/capture/command.c` | 1.5h |
| 3.5 | Add `captureStarted`/`captureCompleted`/`captureFailed` signal emission from ShaulaService | Modified: `integrations/quickshell/shaula-core/ShaulaService.qml` | 1h |
| 3.6 | Write integration test: capture lifecycle → IPC signals → widget state changes | New: `scripts/qa/assert-quickshell-capture-lifecycle.sh` | 2h |
| | | **Phase total** | **~8.5h** |

### Phase 4: Standalone Distribution & Setup

**Goal**: Make the standalone widget installable for any QuickShell config.

| # | Task | Files | Effort |
|---|------|-------|--------|
| 4.1 | Write `integrations/quickshell/shaula-standalone/setup-guide.md` | New | 1h |
| 4.2 | Add `shaula setup --quickshell` option for standalone widget install | Modified: `src/main.c and scripts/install.sh` | 2h |
| 4.3 | Detect QuickShell configs (not just Noctalia) in setup wizard | Modified: `src/main.c and scripts/install.sh` | 1.5h |
| 4.4 | Add standalone widget doctor checks | Modified: `src/main.c` | 1h |
| 4.5 | Create example `shell.qml` that includes Shaula standalone widget | New: `integrations/quickshell/examples/minimal-shell/` | 1h |
| 4.6 | Update release workflow to package `integrations/quickshell/` | Modified: `.github/workflows/release.yml` | 1h |
| 4.7 | Remove Bash adapter scripts (replaced by ShaulaService QML) | Deleted: `integrations/noctalia/noctalia-action-adapter.sh`, `noctalia-plugin-poc.sh` | 0.5h |
| 4.8 | Update CONTEXT.md, roadmap, README | Modified: various | 1h |
| | | **Phase total** | **~9h** |

---

## 6. File Tree (Final)

```
integrations/
├── noctalia/
│   └── shaula/
│       ├── manifest.json          # Noctalia plugin manifest (unchanged schema)
│       ├── BarWidget.qml          # Refactored: uses ShaulaService for captures
│       └── README.md              # Updated: references shaula-core
│
├── quickshell/
│   ├── README.md                  # Integration guide for all tiers
│   ├── shaula-core/
│   │   ├── qmldir                 # module declaration: ShaulaService singleton
│   │   └── ShaulaService.qml      # Core: IpcHandler + Process + state
│   │
│   ├── shaula-standalone/
│   │   ├── BarWidget.qml          # Generic QS bar widget (QtQuick.Controls)
│   │   └── README.md              # Setup guide for non-Noctalia QS configs
│   │
│   └── examples/
│       └── minimal-shell/
│           ├── shell.qml           # Example: minimal QS bar with Shaula
│           └── README.md
│
src/
├── ipc/
│   ├── command.c                # `shaula ipc` subcommand dispatcher
│   └── bridge.c                 # QS IPC bridge (call/listen/prop/available)
│
├── capture/
│   ├── command.c     # Extended: IPC handshake path
│   ├── command.c        # Extended: --panel-handshake flag
│   └── command.c              # Extended: panelShow IPC call after capture
│
├── compositor/
│   └── runtime.c                # Extended: quickshellIpcAvailable()
│
├── setup/
│   └── command.c                # Extended: --quickshell, QS detection
│
├── doctor/
│   └── diagnostics.c            # Extended: QS IPC readiness checks
│
└── capabilities/                   # Extended: report IPC availability
```

---

## 7. Risk Analysis

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| QS IPC socket path varies between QS versions | High | Medium | Detect socket via `qs ipc show` (canonical); abstract behind `shaula ipc` bridge |
| Noctalia plugin import path breaks when referencing `shaula-core` | High | Low | Test with Noctalia's `PluginService` loader; use relative `../../quickshell/shaula-core` import verified at load time |
| `IpcHandler` introspection changes across QS releases | Medium | Medium | Pin minimum QS version in docs; use only stable `qs ipc call/listen/prop` CLI interface |
| Race condition: capture starts before panel fully hides | Medium | Low | IPC `panelHide()` call is synchronous from QS side; guard reads `panelHidden` prop after call returns |
| Standalone widget looks poor without a design system | Low | High | Use QtQuick.Controls Fusion style; document that Noctalia provides the premium experience |
| Bash adapter removal breaks external integrations | Medium | Low | Phase 4 only removes after ShaulaService covers all 8 actions; deprecation warning in v0.2.x |
| Multiple QuickShell instances compete for `shaula` IPC target | Low | Low | QS warns on duplicate IpcHandler targets; document single-instance expectation |

---

## 8. Compatibility & Migration

### Backward Compatibility

- **Phase 0-1**: Zero behavior change. Noctalia plugin works identically; `ShaulaService` is an internal refactor.
- **Phase 2**: New IPC handshake is **additive** — the file-token path remains as fallback. `--panel-handshake=auto` tries IPC first.
- **Phase 3**: Capture state feedback is **additive** — widgets that don't read `captureInProgress` work unchanged.
- **Phase 4**: Bash adapters are deprecated but kept for one release cycle before removal.

### Migration Path for Existing Noctalia Users

1. `shaula setup` continues to install the Noctalia plugin as before.
2. After Phase 1, the installed `BarWidget.qml` uses `ShaulaService` internally — no user action needed.
3. After Phase 2, capture automatically prefers IPC handshake when QuickShell is detected.
4. No manual config changes required at any phase.

---

## 9. Testing Strategy

| Layer | What | How |
|-------|------|-----|
| **Unit** | `ShaulaService.qml` function/signal/property contract | QML test runner (`qtqmltest`) or manual shell |
| **Contract** | IPC bridge: `shaula ipc call/listen/prop` ↔ `qs ipc` | Bash contract tests in `scripts/qa/` |
| **Contract** | Panel-hide handshake: IPC vs file-token vs settle | `assert-quickshell-ipc-handshake.sh` |
| **Integration** | Noctalia plugin loads, menu actions dispatch captures | Manual: `./dev capture` with Noctalia running |
| **Integration** | Standalone widget in minimal QS shell | Manual: `qs -p integrations/quickshell/examples/minimal-shell/` |
| **Regression** | Existing Noctalia QA scripts still pass | `scripts/qa/assert-noctalia-*.sh` |
| **E2E** | Full capture lifecycle: IPC handshake → capture → state sync → panel restore | Manual on real Niri + Noctalia |

---

## 10. References

### QuickShell Source

| Resource | URL |
|----------|-----|
| QuickShell repo (mirror) | https://github.com/quickshell-mirror/quickshell |
| Core module API | https://github.com/quickshell-mirror/quickshell/blob/master/src/core/module.md |
| IpcHandler C++ source | https://github.com/quickshell-mirror/quickshell/blob/master/src/io/ipchandler.hpp |
| IpcHandler implementation | https://github.com/quickshell-mirror/quickshell/blob/master/src/io/ipchandler.cpp |
| IPC wire protocol | https://github.com/quickshell-mirror/quickshell/blob/master/src/io/ipccomm.hpp |
| IPC wire implementation | https://github.com/quickshell-mirror/quickshell/blob/master/src/io/ipccomm.cpp |
| Process QML type | https://github.com/quickshell-mirror/quickshell/blob/master/src/io/process.hpp |
| PanelWindow interface | https://github.com/quickshell-mirror/quickshell/blob/master/src/window/panelinterface.hpp |
| Variants (per-screen) | https://github.com/quickshell-mirror/quickshell/blob/master/src/core/variants.hpp |
| Build instructions | https://github.com/quickshell-mirror/quickshell/blob/master/BUILD.md |

### Noctalia Source

| Resource | URL |
|----------|-----|
| Noctalia shell | https://github.com/noctalia-dev/noctalia-shell |
| PluginRegistry | https://github.com/noctalia-dev/noctalia-shell/blob/main/Services/Noctalia/PluginRegistry.qml |
| PluginService | https://github.com/noctalia-dev/noctalia-shell/blob/main/Services/Noctalia/PluginService.qml |
| BarWidgetLoader | https://github.com/noctalia-dev/noctalia-shell/blob/main/Modules/Bar/Extras/BarWidgetLoader.qml |
| Noctalia plugins repo | https://github.com/noctalia-dev/noctalia-plugins |
| Example plugin manifest | https://github.com/noctalia-dev/noctalia-plugins/blob/main/air-quality/manifest.json |

### Community QuickShell Configs (Reference Patterns)

| Resource | URL | Pattern |
|----------|-----|---------|
| imiric/quickshell-niri | https://github.com/imiric/quickshell-niri | Niri IPC, relative imports, singleton services |
| parth-sarthi-code/quickshell-niri-panel | https://github.com/parth-sarthi-code/quickshell-niri-panel | qmldir modules, control center PanelWindow |
| doannc2212/quickshell-config | https://github.com/doannc2212/quickshell-config | IpcHandler, Variants per-screen, Process |
| MystiaFin/shell | https://github.com/MystiaFin/shell | Simple IpcHandler toggle |
| Cu3PO42/gleaming-glacier | https://github.com/Cu3PO42/gleaming-glacier | IpcHandler with args, `qs ipc call` examples |
| noctalia-plugins/ds4-colors | https://github.com/noctalia-dev/noctalia-plugins/tree/main/ds4-colors | Noctalia plugin with IPC: `qs ipc call plugin:ds4-colors setColor 255 0 0` |

### Shaula Internal References

| Resource | Path |
|----------|------|
| Existing Noctalia plugin | `integrations/noctalia/shaula/` |
| Noctalia action adapter | `integrations/noctalia/noctalia-action-adapter.sh` |
| Setup installer (Noctalia path) | `src/main.c and scripts/install.sh:275-500` |
| Panel-hide precondition guard | `src/capture/command.c` |
| Compositor detection | `src/compositor/runtime.{c,h}` |
| Doctor diagnostics | `src/main.c` |
| Capture lifecycle | `src/capture/command.c` |
| Capture command grammar | `src/capture/command.c` |
| Architecture spec | `spec/architecture.md` |
| Wayland/Niri integration spec | `spec/wayland-niri-integration.md` |
| CONTEXT.md | `CONTEXT.md` |

---

## 11. Open Questions

1. **QML module import path**: When Noctalia loads a plugin from `~/.config/noctalia/plugins/shaula/`, can `BarWidget.qml` import `../../quickshell/shaula-core`? Or does the QML engine resolve imports relative to the shell root, not the plugin file? **Investigation needed**: Test with Noctalia's `Qt.createComponent()` to verify relative import resolution from plugin directories.

2. **`ShaulaService` packaging**: Should `shaula-core/` be installed alongside the Noctalia plugin (inside `~/.config/noctalia/plugins/shaula/shaula-core/`), or as a shared QS module at `~/.config/quickshell/modules/shaula-core/`? The former keeps Noctalia self-contained; the latter enables standalone use.

3. **IPC socket discovery**: How does `qs ipc` find the socket? Is it at `$XDG_RUNTIME_DIR/quickshell.<instance>/ipc.sock`? The `shaula ipc` bridge needs to locate it reliably, or delegate to `qs ipc` CLI.

4. **Process stdout capture**: `Quickshell.Io.Process` can capture stdout via `StdioCollector`. Should `ShaulaService` parse the `--json` output, or just track exit code? Parsing JSON in QML requires `JSON.parse()` which is available but adds complexity.

5. **Minimum QuickShell version**: Which QS release introduced `IpcHandler`? Need to document minimum version for IPC features and provide graceful degradation for older QS builds.

6. **Multiple capture sessions**: What happens if the user triggers a second capture while the first is still in progress? `ShaulaService` should reject (matching existing `ERR_CAPTURE_IN_PROGRESS`), but the IPC call return value semantics need definition.

---

## 12. Success Criteria

- [ ] Noctalia plugin works identically after refactor (zero regression)
- [ ] `ShaulaService.qml` registers `IpcHandler` target `"shaula"` and responds to `qs ipc call`
- [ ] Panel-hide handshake works via IPC with lower latency than file-token polling
- [ ] `shaula ipc available` correctly detects QuickShell IPC presence
- [ ] Standalone widget loads in any QuickShell config without Noctalia dependencies
- [ ] Capture-in-progress indicator reflects real state on both Noctalia and standalone widgets
- [ ] All existing QA scripts pass
- [ ] CONTEXT.md updated with QuickShell integration architecture
