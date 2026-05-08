import QtQuick
import Quickshell
import qs.Commons
import qs.Modules.Bar.Extras
import qs.Services.UI
import qs.Widgets

NIconButton {
  id: root

  property var pluginApi
  property ShellScreen screen
  property string widgetId: ""
  property string section: ""
  property int sectionWidgetIndex: -1
  property int sectionWidgetsCount: 0

  readonly property string screenName: screen ? screen.name : ""
  readonly property int captureDelayMs: 180

  icon: "camera"
  tooltipText: "Shaula"
  tooltipDirection: BarService.getTooltipDirection(screenName)
  baseSize: Style.getCapsuleHeightForScreen(screenName)
  applyUiScale: false
  customRadius: Style.radiusL
  colorBg: Style.capsuleColor
  colorFg: Color.mOnSurface
  colorBgHover: Color.mHover
  colorFgHover: Color.mOnHover
  colorBorder: Style.capsuleBorderColor
  colorBorderHover: Style.capsuleBorderColor

  NPopupContextMenu {
    id: contextMenu

    model: [
      {
        "label": "Capture Area",
        "action": "capture-area",
        "icon": "crop"
      },
      {
        "label": "Capture Fullscreen",
        "action": "capture-fullscreen",
        "icon": "screen-share"
      },
      {
        "label": "Capture Focused Output",
        "action": "capture-focused",
        "icon": "focus"
      },
      {
        "label": "Run Doctor",
        "action": "doctor",
        "icon": "stethoscope"
      },
      {
        "label": "Open Screenshots Folder",
        "action": "open-screenshots-folder",
        "icon": "folder"
      }
    ]

    onTriggered: action => {
      contextMenu.close();
      PanelService.closeContextMenu(screen);
      root.runShaula(action, true);
    }
  }

  Timer {
    id: delayedAction
    repeat: false
    property string action: ""
    onTriggered: {
      if (action !== "")
        root.executeAction(action);
      action = "";
    }
  }

  onClicked: root.runShaula("capture-area", false)
  onRightClicked: PanelService.showContextMenu(contextMenu, root, screen)

  function runShaula(action, fromMenu) {
    if (fromMenu) {
      delayedAction.action = action;
      delayedAction.interval = captureDelayMs;
      delayedAction.restart();
      return;
    }
    executeAction(action);
  }

  function executeAction(action) {
    var command = "";
    if (action === "capture-area") {
      command = "shaula capture area --json";
    } else if (action === "capture-fullscreen") {
      command = "shaula capture fullscreen --json";
    } else if (action === "capture-focused") {
      command = "shaula capture focused --json";
    } else if (action === "doctor") {
      command = "shaula doctor --json";
    } else if (action === "open-screenshots-folder") {
      command = "mkdir -p \"$HOME/Pictures/Shaula\" && xdg-open \"$HOME/Pictures/Shaula\"";
    }

    if (command !== "")
      Quickshell.execDetached(["sh", "-lc", command]);
  }
}
