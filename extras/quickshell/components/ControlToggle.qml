import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// A Control Centre toggle: a round icon button (accent while on) with a
// title and a detail line. Clicking the circle toggles; clicking the text
// emits `opened` (e.g. to show that module's full panel) when `openable`.
Item {
    id: root

    property string icon
    property string text
    property string detail: ""
    property bool checked: false
    property bool openable: false
    signal toggled()
    signal opened()

    Layout.fillWidth: true
    implicitHeight: 34

    Rectangle {
        id: knob
        width: 30; height: 30; radius: 15
        anchors.verticalCenter: parent.verticalCenter
        color: root.checked ? Theme.p.accent : Theme.veil(knobMouse.containsMouse ? 0.16 : 0.10)
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
        Icon { anchors.centerIn: parent; name: root.icon; size: 15; color: root.checked ? "white" : Theme.p.text }
        MouseArea { id: knobMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled() }
    }

    ColumnLayout {
        anchors { left: knob.right; leftMargin: 8; right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: 0
        Text {
            Layout.fillWidth: true
            text: root.text
            color: Theme.p.text
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSize - 1
            font.family: Theme.fontFamily
            font.weight: Font.DemiBold
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

    MouseArea {
        anchors { left: knob.right; right: parent.right; top: parent.top; bottom: parent.bottom }
        cursorShape: root.openable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.openable ? root.opened() : root.toggled()
    }
}
