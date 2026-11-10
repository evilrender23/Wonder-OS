import QtQuick

/// A line of the inspector: what it is, and the controls for it.
Item {
    id: row
    property string label
    default property alias content: holder.data
    width: parent ? parent.width : 0
    height: Math.max(28, holder.implicitHeight)
    Text {
        width: 78
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: Theme.dim
        font.pixelSize: 12
        elide: Text.ElideRight
    }
    Row {
        id: holder
        x: 84
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
    }
}
