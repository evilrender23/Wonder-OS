import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Notifications
import ".."
import "../services"
import "../components"

// Notification toasts under the bar's right end, on the primary screen.
// Each slides in, waits its timeout (critical ones stay) and slides out;
// hovering pauses the timer.
PanelWindow {
    id: root

    screen: Quickshell.screens[0]
    WlrLayershell.namespace: "wonderland-toasts"
    WlrLayershell.layer: WlrLayer.Overlay
    anchors { top: true; right: true }
    margins { top: 6; right: 8 }
    exclusionMode: ExclusionMode.Ignore
    color: "transparent"
    implicitWidth: 360
    implicitHeight: Math.max(1, stack.implicitHeight)
    // A bar panel open under the bar would sit beneath the toasts; step
    // aside until it closes (timers pause meanwhile).
    readonly property bool paused: Popups.current !== null
    visible: Notifs.popups.length > 0 && !paused
    mask: Region { item: stack }

    ColumnLayout {
        id: stack
        width: parent.width
        spacing: 8

        Repeater {
            model: Notifs.popups
            delegate: Item {
                id: slot
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight

                NotificationCard {
                    id: card
                    width: parent.width
                    entry: slot.modelData
                    toast: true
                    x: shown ? 0 : root.width
                    property bool shown: false
                    Component.onCompleted: shown = true
                    Behavior on x { NumberAnimation { duration: Theme.animMs * 2; easing.type: Easing.OutCubic } }

                    HoverHandler { id: hovered }
                }

                Timer {
                    readonly property int timeout: slot.modelData.notification && slot.modelData.notification.expireTimeout > 0
                        ? slot.modelData.notification.expireTimeout : 6000
                    interval: timeout
                    running: slot.modelData.urgency !== NotificationUrgency.Critical && !hovered.hovered && !root.paused
                    onTriggered: Notifs.hidePopup(slot.modelData)
                }
            }
        }
    }
}
