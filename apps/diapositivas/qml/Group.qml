import QtQuick

/// A titled part of the inspector.
Column {
    id: group
    property string title
    default property alias content: body.data
    width: parent ? parent.width : 0
    spacing: 8
    Text {
        text: group.title
        color: Theme.dim
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.4
    }
    Column {
        id: body
        width: parent.width
        spacing: 8
    }
    Item { width: 1; height: 4 }
    Rectangle { width: parent.width; height: 1; color: Theme.line }
}
