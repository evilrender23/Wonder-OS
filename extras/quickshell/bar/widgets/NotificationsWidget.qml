import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import "../.."
import "../../services"
import "../../components"

// Notification history and do-not-disturb. Right click toggles DND.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "notifications"
    icon: Notifs.dnd ? "bell-off" : Notifs.count > 0 ? "bell-dot" : "bell"
    text: Notifs.count > 0 ? `${Notifs.count}` : ""
    iconColor: Notifs.count > 0 && !Notifs.dnd ? Theme.p.accent : Theme.p.text2
    active: panel.visible
    tooltip: Notifs.dnd ? "Do not disturb" : Notifs.count ? `${Notifs.count} notifications` : "No notifications"
    onClicked: panel.toggle()
    onRightClicked: Settings.data.doNotDisturb = !Settings.data.doNotDisturb

    BarPanel {
        id: panel
        target: root
        title: "Notifications"
        icon: "bell"
        contentWidth: 360

        headerExtra: IconButton { visible: Notifs.count > 0; icon: "trash-2"; size: 24; tooltip: "Clear all"; onClicked: Notifs.clearAll() }

        Switch {
            text: "Do not disturb"
            detail: "Only critical notifications pop up"
            checked: Notifs.dnd
            onToggled: v => Settings.data.doNotDisturb = v
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 420)
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: list
                width: parent.width
                spacing: 6
                Repeater {
                    model: Notifs.history
                    delegate: NotificationCard { required property var modelData; entry: modelData; Layout.fillWidth: true }
                }
            }
        }
        Label { visible: Notifs.count === 0; role: "muted"; text: "You're all caught up."; Layout.alignment: Qt.AlignHCenter }
        Label { visible: !Notifs.serverActive; role: "caption"; color: Theme.p.red; text: "Another notification daemon owns the bus." }
    }
}
