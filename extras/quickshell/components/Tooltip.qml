import QtQuick
import Quickshell
import ".."
import "../services"

// A small label under `target` after a short hover delay.
PopupWindow {
    id: root

    required property Item target
    property string text
    property bool shown: false

    anchor.item: target
    anchor.edges: Edges.Bottom
    anchor.gravity: Edges.Bottom
    anchor.margins.top: 6
    color: "transparent"
    implicitWidth: label.implicitWidth + 16
    implicitHeight: label.implicitHeight + 10
    visible: false

    onShownChanged: shown ? delay.restart() : (delay.stop(), visible = false)

    Timer {
        id: delay
        interval: 450
        onTriggered: if (root.shown && root.text !== "") root.visible = true
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.dark ? Theme.p.elevated : Theme.p.text
        border.width: Theme.dark ? 1 : 0
        border.color: Theme.p.stroke2

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.dark ? Theme.p.text : Theme.p.chrome
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
        }
    }
}
