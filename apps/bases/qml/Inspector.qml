import QtQuick
import QtQuick.Controls.Basic

/// The panel on the right: the properties of what is picked.
Rectangle {
    id: panel
    property string heading: "Formato"
    default property alias content: body.data
    width: 264
    color: Theme.chrome
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }
    Text {
        id: title
        x: 16
        y: 12
        width: parent.width - 32
        text: panel.heading
        color: Theme.text
        font.pixelSize: 13
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Flickable {
        anchors.fill: parent
        anchors.topMargin: 38
        anchors.leftMargin: 1
        contentHeight: body.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: body
            x: 15
            width: panel.width - 31
            spacing: 8
        }
    }
}
