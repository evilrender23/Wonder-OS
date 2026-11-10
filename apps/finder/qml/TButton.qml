import QtQuick

/// A toolbar button: one glyph, a quiet hover.
Rectangle {
    id: button
    property string symbol
    property bool checked: false
    property bool active: true
    property string tip
    signal clicked()
    width: 30
    height: 26
    radius: 6
    color: checked ? Theme.sideSelection : (area.containsMouse && active ? Theme.hover : "transparent")
    Sym {
        anchors.centerIn: parent
        name: button.symbol
        color: button.active ? Theme.text : Theme.faint
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (button.active) button.clicked()
    }
}
