import QtQuick

/// A few cells in small: the miniature of a template or of a version.
Rectangle {
    id: mini
    /// Rows of [{text, fill, bold, color, big, number}].
    property var rows: []
    property real rowHeight: 12
    property real fontSize: 7
    readonly property int columns: rows.length > 0 ? rows[0].length : 0
    color: "#ffffff"
    clip: true
    Column {
        anchors.fill: parent
        anchors.margins: 6
        Repeater {
            model: mini.rows
            Row {
                id: line
                required property var modelData
                Repeater {
                    model: line.modelData
                    Rectangle {
                        required property var modelData
                        width: (mini.width - 12) / Math.max(1, mini.columns)
                        height: mini.rowHeight
                        color: modelData.fill ? modelData.fill : "transparent"
                        // A title runs over the cells beside it, as it does on the sheet.
                        z: modelData.big ? 1 : 0
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#efeff1"; visible: !parent.modelData.fill }
                        Text {
                            x: 2
                            width: parent.modelData.big ? implicitWidth : parent.width - 4
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: parent.modelData.number ? Text.AlignRight : Text.AlignLeft
                            text: parent.modelData.text
                            elide: parent.modelData.big ? Text.ElideNone : Text.ElideRight
                            color: parent.modelData.color ? parent.modelData.color : "#1d1d1f"
                            font.pixelSize: parent.modelData.big ? mini.fontSize + 2 : mini.fontSize
                            font.weight: parent.modelData.bold ? Font.Bold : Font.Normal
                        }
                    }
                }
            }
        }
    }
}
