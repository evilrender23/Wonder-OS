import QtQuick
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Canvas zoom. Click: fit everything / back; right click: 100 %; scroll: zoom.
Pill {
    id: root

    property var bar
    widgetId: "zoom"
    icon: Compositor.zoom < 0.99 ? "zoom-out" : "zoom-in"
    text: `${Math.round(Compositor.zoom * 100)}%`
    tooltip: "Canvas zoom · click: overview · right click: 100%"
    onClicked: Compositor.action("zoom-to-fit")
    onRightClicked: Compositor.action("zoom-reset")
    onScrolled: delta => Compositor.action(delta > 0 ? "zoom-in" : "zoom-out")
}
