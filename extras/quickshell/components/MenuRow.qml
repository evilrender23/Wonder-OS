import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// One row of a macOS-style menu: label on the left, an optional shortcut hint
// on the right, and the whole row filled with the accent colour on hover.
// `armed` turns the row red (destructive entries asking for confirmation).
Item {
    id: root

    property string text
    property string shortcut: ""
    property bool armed: false
    property bool enabled: true
    readonly property bool hovered: mouse.containsMouse && enabled
    signal triggered()

    Layout.fillWidth: true
    implicitHeight: 24
    implicitWidth: label.implicitWidth + hint.implicitWidth + 40

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.armed ? Theme.p.red : Theme.p.accent
        visible: root.hovered || root.armed
    }

    Text {
        id: label
        anchors { left: parent.left; leftMargin: 10; right: hint.visible ? hint.left : parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
        elide: Text.ElideRight
        text: root.text
        color: root.hovered || root.armed ? Theme.p.onAccent : root.enabled ? Theme.p.text : Theme.p.text3
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
    }

    Text {
        id: hint
        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
        text: root.shortcut
        visible: root.shortcut !== ""
        color: root.hovered || root.armed ? Theme.p.onAccent : Theme.p.text3
        opacity: root.hovered || root.armed ? 0.85 : 1
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.triggered()
    }
}
