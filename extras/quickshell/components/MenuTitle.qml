import QtQuick
import ".."
import "../services"

// A title in the menu bar ("Ventana", "Lienzo"…): plain text with the
// macOS rounded highlight while hovered or while its menu is open. `bold` is
// the app name. Hovering a title while another menu of the same bar is open
// switches to it, as on macOS.
Item {
    id: root

    property string text
    property bool bold: false
    property bool open: false
    readonly property bool hovered: mouse.containsMouse
    signal pressed()
    signal entered()

    implicitWidth: label.implicitWidth + 18
    implicitHeight: Theme.pillHeight

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.open ? Theme.veil(0.16) : root.hovered ? Theme.veil(0.08) : "transparent"
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: Theme.p.text
        font.pixelSize: Theme.fontSize
        font.family: Theme.fontFamily
        font.weight: root.bold ? Font.Bold : Font.Medium
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onPressed: root.pressed()
        onEntered: root.entered()
    }
}
