import QtQuick

/// A bar button with a word beside its glyph.
Rectangle {
    id: button
    property string symbol
    property string text
    property bool active: true
    property bool checked: false
    property bool strong: false
    signal clicked()
    implicitWidth: row.implicitWidth + 18
    implicitHeight: 26
    radius: 6
    color: strong ? Theme.tint : (checked ? Theme.sideSelection : (area.containsMouse && active ? Theme.hover : "transparent"))
    opacity: !active ? 0.45 : (area.pressed ? 0.8 : 1)
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Sym {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.symbol !== ""
            name: button.symbol
            size: 14
            color: button.strong ? "#ffffff" : Theme.text
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: text !== ""
            text: button.text
            color: button.strong ? "#ffffff" : Theme.text
            font.pixelSize: 13
            font.weight: button.strong ? Font.DemiBold : Font.Normal
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (button.active) button.clicked()
    }
}
