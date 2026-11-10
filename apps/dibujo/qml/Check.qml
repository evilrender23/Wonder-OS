import QtQuick

/// A box to tick, with what it means beside it.
Row {
    id: check
    property bool on: false
    property string text
    signal toggled(bool on)
    spacing: 7
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 4
        color: check.on ? Theme.tint : Theme.field
        border.color: check.on ? "transparent" : Theme.border
        Sym {
            anchors.centerIn: parent
            visible: check.on
            name: "check"
            size: 12
            color: "#ffffff"
        }
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: check.text
        color: Theme.text
        font.pixelSize: 13
    }
    TapHandler {
        onTapped: check.toggled(!check.on)
    }
}
