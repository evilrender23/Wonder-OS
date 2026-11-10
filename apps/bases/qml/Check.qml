import QtQuick

/// A box to tick, with its label.
Item {
    id: box
    property bool checked: false
    property string text
    property bool active: true
    signal toggled(bool checked)
    implicitWidth: 16 + (label.text === "" ? 0 : 7 + label.implicitWidth)
    implicitHeight: 22
    opacity: active ? 1 : 0.45
    Rectangle {
        id: square
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 4
        color: box.checked ? Theme.tint : Theme.field
        border.color: box.checked ? Theme.tint : Theme.border
        Sym {
            anchors.centerIn: parent
            visible: box.checked
            name: "check"
            size: 12
            color: "#ffffff"
        }
    }
    Text {
        id: label
        anchors.left: square.right
        anchors.leftMargin: 7
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: box.text
        color: Theme.text
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    MouseArea {
        anchors.fill: parent
        onClicked: if (box.active) box.toggled(!box.checked)
    }
}
