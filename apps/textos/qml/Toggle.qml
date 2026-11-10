import QtQuick

/// On or off, said with a small switch.
Rectangle {
    id: toggle
    property bool checked: false
    signal toggled(bool checked)
    implicitWidth: 34
    implicitHeight: 20
    radius: 10
    color: checked ? Theme.tint : (Theme.dark ? "#48484a" : "#d1d1d6")
    Behavior on color { ColorAnimation { duration: 120 } }
    Rectangle {
        x: toggle.checked ? toggle.width - width - 2 : 2
        y: 2
        width: 16
        height: 16
        radius: 8
        color: "#ffffff"
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: toggle.toggled(!toggle.checked)
    }
}
