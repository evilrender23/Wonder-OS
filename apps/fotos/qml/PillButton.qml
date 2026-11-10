import QtQuick

/// «Añadir carpeta…»: a glyph and a word in the app's colour.
Rectangle {
    id: button
    property string symbol
    property string text
    signal clicked()
    implicitWidth: Math.max(112, row.implicitWidth + 28)
    implicitHeight: 30
    radius: 8
    color: area.pressed ? Theme.sideSelection : (area.containsMouse ? Theme.hover : Theme.field)
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Sym {
            anchors.verticalCenter: parent.verticalCenter
            name: button.symbol
            size: 13
            color: Theme.tint
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: Theme.tint
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
