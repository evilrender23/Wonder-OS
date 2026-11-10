import QtQuick

/// A row of colours to pick one from. An empty string is «none».
Row {
    id: row
    property var colors: []
    property string current
    signal picked(string color)
    spacing: 7
    Repeater {
        model: row.colors
        Rectangle {
            required property string modelData
            readonly property bool on: modelData === "" ? row.current === "" : row.current.toLowerCase() === modelData.toLowerCase()
            width: 22
            height: 22
            radius: 11
            color: "transparent"
            border.width: on ? 2 : 0
            border.color: Theme.tint
            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 16
                radius: 8
                color: parent.modelData === "" ? Theme.window : parent.modelData
                border.color: Theme.dark ? "#33ffffff" : "#26000000"
                Rectangle {
                    visible: parent.parent.modelData === ""
                    anchors.centerIn: parent
                    width: 16
                    height: 1.5
                    rotation: -45
                    color: "#ff3b30"
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: row.picked(parent.modelData)
            }
        }
    }
}
