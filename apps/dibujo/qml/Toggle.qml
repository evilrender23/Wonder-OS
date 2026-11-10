import QtQuick

/// On or off.
Rectangle {
    id: toggle
    property bool on: false
    signal toggled(bool on)
    width: 34
    height: 20
    radius: 10
    color: on ? Theme.tint : Theme.sideSelection
    Behavior on color { ColorAnimation { duration: 100 } }
    Rectangle {
        x: toggle.on ? parent.width - width - 2 : 2
        y: 2
        width: 16
        height: 16
        radius: 8
        color: "#ffffff"
        Behavior on x { NumberAnimation { duration: 100 } }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: toggle.toggled(!toggle.on)
    }
}
