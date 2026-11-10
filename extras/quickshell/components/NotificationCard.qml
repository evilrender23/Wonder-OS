import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.Notifications
import ".."
import "../services"

// One notification: app icon/image, summary, body, actions and a ×. Used by
// the history panel and the toasts.
Rectangle {
    id: root

    required property var entry
    property bool toast: false
    signal dismissed()

    readonly property bool critical: entry && entry.urgency === NotificationUrgency.Critical
    implicitHeight: col.implicitHeight + 20
    implicitWidth: 340
    radius: Theme.popupRadius
    color: toast ? Theme.p.elevated : Theme.p.surface
    border.width: 1
    border.color: critical ? Theme.p.red : Theme.p.stroke2

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            const def = root.entry.actions.find(a => a.identifier === "default")
            if (def) Notifs.invoke(root.entry, def)
        }
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 32
                implicitHeight: 32
                Rectangle { anchors.fill: parent; radius: 6; color: Theme.p.track; visible: !img.visible && !appIcon.visible }
                Icon { anchors.centerIn: parent; name: "bell"; visible: !img.visible && !appIcon.visible }
                Image {
                    id: img
                    anchors.fill: parent
                    source: root.entry ? root.entry.image : ""
                    visible: source != "" && status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                }
                IconImage {
                    id: appIcon
                    anchors.fill: parent
                    visible: !img.visible && root.entry && root.entry.appIcon !== ""
                    source: root.entry && root.entry.appIcon ? Quickshell.iconPath(root.entry.appIcon, true) : ""
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Layout.rightMargin: 22          // room for the ×
                    Label { role: "caption"; text: root.entry ? root.entry.appName : ""; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { role: "caption"; text: root.entry ? Qt.formatTime(root.entry.time, Settings.data.use24h ? "HH:mm" : "h:mm ap") : "" }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.entry ? root.entry.summary : ""
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    role: "muted"
                    text: root.entry ? root.entry.body : ""
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                    maximumLineCount: root.toast ? 3 : 6
                    elide: Text.ElideRight
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: root.entry && root.entry.actions.filter(a => a.identifier !== "default").length > 0
            Repeater {
                model: root.entry ? root.entry.actions.filter(a => a.identifier !== "default") : []
                delegate: IconButton {
                    required property var modelData
                    text: modelData.text
                    size: 26
                    onClicked: Notifs.invoke(root.entry, modelData)
                }
            }
        }
    }

    IconButton {
        anchors { top: parent.top; right: parent.right; margins: 6 }
        visible: hover.containsMouse || root.toast
        icon: "x"
        size: 20
        tooltip: root.toast ? "Hide" : "Dismiss"
        onClicked: {
            if (root.toast) { Notifs.hidePopup(root.entry); root.dismissed() }
            else Notifs.dismiss(root.entry)
        }
    }
}
