import QtQuick
import QtQuick.Controls.Basic

/// The pill that floats by what is selected, with what is most likely wanted.
Rectangle {
    id: pill

    /// [{symbol, tip, verb, active}]
    property var entries: []
    signal chosen(string verb)

    width: row.width + 12
    height: 36
    radius: 18
    color: Theme.dark ? "#3a3a3d" : "#ffffff"
    border.color: Theme.dark ? "#30ffffff" : "#24000000"

    // A shadow, as far as a rectangle can cast one.
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 1
        anchors.bottomMargin: -3
        radius: parent.radius + 1
        color: "#18000000"
    }
    // Presses do not reach the grid below.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }
    Row {
        id: row
        x: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Repeater {
            model: pill.entries
            TButton {
                required property var modelData
                symbol: modelData.symbol
                size: 15
                active: modelData.active !== false
                onClicked: pill.chosen(modelData.verb)
                ToolTip.visible: tipHover.hovered
                ToolTip.delay: 500
                ToolTip.text: modelData.tip
                HoverHandler { id: tipHover }
            }
        }
    }
}
