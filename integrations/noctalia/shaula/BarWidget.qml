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
        "label": "Capture All Screens",
        "action": "capture-all-screens",
        "icon": "layout-dashboard"
      },
      {
        "label": "Settings",
        "action": "settings",
        "icon": "settings",
        "enabled": true
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
      },
      {
        "label": "Report a Bug",
        "action": "report-bug",
        "icon": "bug"
      }
    ]

    onTriggered: action => {
      contextMenu.close();
      PanelService.closeContextMenu(screen);
      root.executeAction(action);
    }
  }

  onClicked: PanelService.showContextMenu(contextMenu, root, screen)
  onRightClicked: PanelService.showContextMenu(contextMenu, root, screen)

  function executeAction(action) {
    var command = "";
    if (action === "capture-area") {
      command = "shaula capture area --json";
    } else if (action === "capture-fullscreen") {
      command = "shaula capture fullscreen --json";
    } else if (action === "capture-all-screens") {
      command = "shaula capture all-screens --json";
    } else if (action === "settings") {
      command = "shaula settings";
    } else if (action === "doctor") {
      command = "shaula doctor --json";
    } else if (action === "open-screenshots-folder") {
      command = "mkdir -p \"$HOME/Pictures/Shaula\" && xdg-open \"$HOME/Pictures/Shaula\"";
    } else if (action === "report-bug") {
      command = "xdg-open https://github.com/fgonzalezurriola/shaula/issues";
    }

    if (command !== "")
      Quickshell.execDetached(["sh", "-lc", command]);
  }
}
