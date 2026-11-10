import QtQuick

/// The three little bars beside the track that is playing.
Row {
    id: bars
    property bool running: false
    property color color: Theme.tint
    spacing: 2
    height: 12
    Repeater {
        model: [0.9, 0.5, 0.7]
        Item {
            id: slot
            required property real modelData
            required property int index
            width: 3
            height: bars.height
            Rectangle {
                id: bar
                width: parent.width
                radius: 1
                color: bars.color
                anchors.bottom: parent.bottom
                height: bars.height * slot.modelData
                SequentialAnimation on height {
                    running: bars.running && bars.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: bars.height * 0.25; duration: 260 + slot.index * 70; easing.type: Easing.InOutSine }
                    NumberAnimation { to: bars.height; duration: 300 + slot.index * 90; easing.type: Easing.InOutSine }
                }
            }
        }
    }
}
