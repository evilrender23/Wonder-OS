import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Session menu. Destructive entries ask once more (click again to confirm).
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "power"
    icon: "power"
    active: panel.visible
    tooltip: "Session"
    onClicked: panel.toggle()

    BarPanel {
        id: panel
        target: root
        title: "Session"
        icon: "power"
        contentWidth: 240
        onVisibleChanged: armed.which = ""

        QtObject { id: armed; property string which: "" }

        Repeater {
            model: [
                { id: "lock", label: "Lock", icon: "lock", confirm: false },
                { id: "suspend", label: "Suspend", icon: "moon", confirm: false },
                { id: "logout", label: "Log out", icon: "log-out", confirm: true },
                { id: "reboot", label: "Restart", icon: "rotate-ccw", confirm: true },
                { id: "poweroff", label: "Shut down", icon: "power", confirm: true }
            ]
            delegate: ListItem {
                required property var modelData
                icon: modelData.icon
                iconColor: armed.which === modelData.id ? Theme.p.red : Theme.p.text2
                text: armed.which === modelData.id ? `${modelData.label}? Click again` : modelData.label
                highlighted: armed.which === modelData.id
                onClicked: {
                    if (modelData.confirm && armed.which !== modelData.id) { armed.which = modelData.id; return }
                    panel.close()
                    Session[modelData.id]()
                }
            }
        }
    }
}
