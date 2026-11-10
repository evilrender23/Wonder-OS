import QtQuick
import QtQuick.Layouts
import Quickshell.Widgets
import ".."
import "../services"

// A list row: optional icon, title, detail, and an optional trailing icon
// button. Highlighted rows get the Hermes active-row tint.
Item {
    id: root

    property string icon: ""
    property string iconSource: ""        // a themed app icon URL instead of a Lucide one
    property color iconColor: Theme.p.text2
    property string text
    property string detail: ""
    property string trailing: ""          // right-aligned small text
    property string actionIcon: ""        // trailing icon button
    property string actionTooltip: ""
    property bool highlighted: false
    property bool clickable: true
    signal clicked()
    signal rightClicked()
    signal actionClicked()

    Layout.fillWidth: true
    implicitHeight: detail !== "" ? 42 : 32

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.highlighted ? Theme.p.active : mouse.containsMouse && root.clickable ? Theme.p.hover : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 9

        Icon { visible: root.icon !== "" && root.iconSource === ""; name: root.icon; color: root.highlighted ? Theme.p.accent : root.iconColor }
        IconImage { visible: root.iconSource !== ""; source: root.iconSource; implicitSize: 20 }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.text
                color: Theme.p.text
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
                font.weight: root.highlighted ? Font.DemiBold : Font.Normal
            }
            Text {
                visible: root.detail !== ""
                Layout.fillWidth: true
                text: root.detail
                color: Theme.p.text3
                elide: Text.ElideRight
                font.pixelSize: Theme.smallFontSize
                font.family: Theme.fontFamily
            }
        }

        Text {
            visible: root.trailing !== ""
            text: root.trailing
            color: Theme.p.text3
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
        }

        IconButton {
            visible: root.actionIcon !== ""
            icon: root.actionIcon
            size: 24
            tooltip: root.actionTooltip
            onClicked: root.actionClicked()
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.rightMargin: root.actionIcon !== "" ? 30 : 0
        hoverEnabled: true
        enabled: root.clickable
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: event => event.button === Qt.RightButton ? root.rightClicked() : root.clicked()
    }
}
