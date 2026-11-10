import QtQuick

/// One entry of the sidebar.
Rectangle {
    id: item
    property string symbol
    property string text
    property bool selected: false
    property color tone: Theme.tint
    signal clicked()
    signal menuRequested()
    width: parent ? parent.width : 0
    height: 30
    radius: 6
    color: selected ? Theme.sideSelection : (area.containsMouse ? Theme.hover : "transparent")
    Sym {
        id: glyph
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        name: item.symbol
        size: 16
        color: item.tone
    }
    Text {
        anchors.left: glyph.right
        anchors.leftMargin: 9
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: item.text
        color: Theme.text
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                item.menuRequested()
            else
                item.clicked()
        }
    }
}
