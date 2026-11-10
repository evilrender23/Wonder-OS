import QtQuick

/// A bar button: one glyph, a quiet hover.
Rectangle {
    id: button
    property string symbol
    property bool checked: false
    property bool active: true
    property int size: 16
    property color tone: Theme.text
    signal clicked()
    width: 30
    height: 26
    radius: 6
    color: area.containsMouse && active ? Theme.hover : "transparent"
    Sym {
        anchors.centerIn: parent
        name: button.symbol
        size: button.size
        color: !button.active ? Theme.faint : (button.checked ? Theme.tint : button.tone)
        scale: area.pressed && button.active ? 0.88 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (button.active) button.clicked()
    }
}
