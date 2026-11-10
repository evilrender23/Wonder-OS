import QtQuick

/// A name on the left, its control on the right.
Item {
    id: row
    property string label
    default property alias content: slot.data
    width: parent ? parent.width : 0
    height: 28
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: Theme.text
        font.pixelSize: 13
    }
    Row {
        id: slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
    }
}
