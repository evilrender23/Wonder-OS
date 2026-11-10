import QtQuick
import Quickshell
import "../.."
import "../../services"
import "../../components"
import "../../panels"

// Focused window's title; click opens the canvas panel (all windows).
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "windows"
    icon: "app-window"
    text: {
        const f = Compositor.focused
        const t = f ? (f.title || f.app_id) : `${Compositor.windows.length} windows`
        return t.length > 48 ? t.slice(0, 47) + "…" : t
    }
    active: panel.visible
    tooltip: Compositor.focused ? Compositor.focused.app_id : ""
    onClicked: panel.toggle()
    onMiddleClicked: if (Compositor.focused) Compositor.closeWindow(Compositor.focused.id)

    CanvasPanel { id: panel; target: root }
}
