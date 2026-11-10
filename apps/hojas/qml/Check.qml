import QtQuick

/// A box to tick.
Rectangle {
    id: box
    property bool on: false
    signal toggled()
    width: 18
    height: 18
    radius: 5
    color: on ? Theme.tint : "transparent"
    border.color: on ? Theme.tint : Theme.faint
    border.width: 1.5
    Sym {
        visible: box.on
        anchors.centerIn: parent
        name: "check"
        size: 13
        color: "#ffffff"
    }
    MouseArea {
        anchors.fill: parent
        anchors.margins: -3
        onClicked: box.toggled()
    }
}
