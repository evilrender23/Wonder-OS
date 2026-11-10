import QtQuick
import QtQuick.Controls.Basic

/// A bar button: one glyph, a quiet hover. `onStage`: drawn over the picture.
Rectangle {
    id: button
    property string symbol
    property bool checked: false
    property bool active: true
    property bool onStage: false
    property int size: 16
    property color tone: onStage ? Theme.ink : Theme.text
    property string tip
    readonly property bool hovered: area.containsMouse
    signal clicked()
    width: 30
    height: 26
    radius: 6
    color: area.containsMouse && active ? (onStage ? Theme.inkHover : Theme.hover) : "transparent"
    Sym {
        anchors.centerIn: parent
        name: button.symbol
        size: button.size
        color: !button.active ? (button.onStage ? Theme.inkFaint : Theme.faint) : (button.checked ? (button.onStage ? Theme.inkTint : Theme.tint) : button.tone)
        scale: area.pressed && button.active ? 0.88 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (button.active) button.clicked()
    }
    ToolTip {
        visible: area.containsMouse && button.tip !== ""
        delay: 700
        text: button.tip
        font.pixelSize: 12
        padding: 6
        contentItem: Text {
            text: button.tip
            font.pixelSize: 12
            color: Theme.text
        }
        background: Rectangle {
            radius: 6
            color: Theme.menu
            border.color: Theme.border
        }
    }
}
