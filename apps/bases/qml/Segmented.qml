import QtQuick

/// «Día  Semana  Mes  Año»: one of a few, all in sight.
Rectangle {
    id: control
    /// [{key, label}]
    property var options: []
    property string current
    signal picked(string key)
    implicitWidth: row.implicitWidth + 4
    implicitHeight: 28
    radius: 7
    color: Theme.field
    Row {
        id: row
        x: 2
        y: 2
        Repeater {
            model: control.options
            Rectangle {
                required property var modelData
                readonly property bool on: control.current === modelData.key
                width: label.implicitWidth + 18
                height: control.height - 4
                radius: 5
                color: on ? (Theme.dark ? "#636366" : "#ffffff") : (area.containsMouse ? Theme.hover : "transparent")
                border.color: on && !Theme.dark ? "#14000000" : "transparent"
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: parent.on ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: control.picked(parent.modelData.key)
                }
            }
        }
    }
}
