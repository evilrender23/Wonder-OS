import QtQuick
import QtQuick.Layouts
import ".."
import "../services"

// Square or labeled button in the Hermes control style: transparent at rest,
// accent-tinted when checked, hover row tint.
Item {
    id: root

    property string icon: ""
    property string text: ""
    property bool checked: false
    property bool primary: false
    property bool danger: false
    property int size: 28
    property string tooltip: ""
    signal clicked()

    implicitHeight: size
    implicitWidth: text === "" ? size : row.implicitWidth + 20
    opacity: enabled ? 1 : 0.45

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.primary ? (mouse.containsMouse ? Theme.p.accentHover : Theme.p.accent)
            : root.checked ? Theme.p.accentSoft
            : mouse.containsMouse ? Theme.p.hover : "transparent"
        border.width: root.checked && !root.primary ? 1 : 0
        border.color: Theme.p.stroke1
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Icon {
            visible: root.icon !== ""
            name: root.icon
            size: Math.round(root.size * 0.55)
            color: root.primary ? Theme.p.onAccent : root.danger ? Theme.p.red
                : root.checked ? Theme.p.accent : Theme.p.text2
        }

        Text {
            visible: root.text !== ""
            text: root.text
            color: root.primary ? Theme.p.onAccent : root.danger ? Theme.p.red : Theme.p.text
            font.pixelSize: Theme.fontSize
            font.family: Theme.fontFamily
            font.weight: root.primary ? Font.DemiBold : Font.Normal
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Tooltip { target: root; text: root.tooltip; shown: mouse.containsMouse && root.tooltip !== "" }
}
