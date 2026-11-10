import QtQuick
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Active keyboard layout from the compositor; click cycles.
Pill {
    id: root

    property var bar
    widgetId: "keyboard"
    shown: Compositor.layoutShort !== ""
    icon: "keyboard"
    text: Compositor.layoutShort.toUpperCase()
    tooltip: "Next layout"
    onClicked: Compositor.action("switch-layout next")
    onRightClicked: Compositor.action("switch-layout prev")
}
