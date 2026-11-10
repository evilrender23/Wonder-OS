import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// Labeled on/off switch row.
Item {
    id: root

    property string text
    property string detail: ""
    property bool checked: false
    signal toggled(bool value)

    implicitHeight: Math.max(30, col.implicitHeight + 6)
    Layout.fillWidth: true

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: knobTrack.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        Text {
            Layout.fillWidth: true
            text: root.text
            color: Theme.p.text
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
            elide: Text.ElideRight
        }
        Text {
            visible: root.detail !== ""
            Layout.fillWidth: true
            text: root.detail
            color: Theme.p.text3
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: knobTrack
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 34; height: 20; radius: 10
        color: root.checked ? Theme.p.accent : Theme.p.track
        border.width: root.checked ? 0 : 1
        border.color: Theme.p.stroke2
        Behavior on color { ColorAnimation { duration: Theme.animMs } }

        Rectangle {
            width: 14; height: 14; radius: 7
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: root.checked ? Theme.p.onAccent : Theme.p.elevated
            border.width: root.checked ? 0 : 1
            border.color: Theme.p.stroke1
            Behavior on x { NumberAnimation { duration: Theme.animMs; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
