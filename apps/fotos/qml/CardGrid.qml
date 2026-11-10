import QtQuick
import QtQuick.Controls.Basic

/// Folders or albums: a cover, a name and how much is inside.
/// `cards`: [{key, name, count, cover, stamp}].
GridView {
    id: grid
    property var cards: []
    property string emptySymbol: "images"
    signal opened(string key)
    signal menuRequested(string key)

    readonly property int columns: Math.max(2, Math.floor((width - 32) / 190))
    readonly property real side: Math.floor((width - 32) / columns)

    clip: true
    leftMargin: 16
    rightMargin: 16
    topMargin: 6
    bottomMargin: 16
    cellWidth: side
    cellHeight: side + 44
    boundsBehavior: Flickable.StopAtBounds
    model: cards
    ScrollBar.vertical: ScrollBar {}

    delegate: Item {
        id: card
        required property var modelData
        width: grid.cellWidth
        height: grid.cellHeight
        Rectangle {
            id: cover
            x: 8
            y: 8
            width: parent.width - 16
            height: width
            radius: 10
            color: Theme.plate
            clip: true
            Image {
                anchors.fill: parent
                source: Theme.thumb(card.modelData.cover, card.modelData.stamp ?? 0)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
            }
            Sym {
                anchors.centerIn: parent
                visible: !card.modelData.cover
                name: grid.emptySymbol
                size: 40
                color: Theme.faint
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.color: area.containsMouse ? Theme.tint : (Theme.dark ? "#22ffffff" : "#14000000")
                border.width: area.containsMouse ? 2 : 1
            }
        }
        Text {
            x: 10
            width: parent.width - 20
            anchors.top: cover.bottom
            anchors.topMargin: 6
            text: card.modelData.name
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.Medium
            elide: Text.ElideMiddle
        }
        Text {
            x: 10
            width: parent.width - 20
            anchors.top: cover.bottom
            anchors.topMargin: 23
            text: card.modelData.count === 1 ? "1 elemento" : card.modelData.count + " elementos"
            color: Theme.dim
            font.pixelSize: 12
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton)
                    grid.menuRequested(card.modelData.key)
                else
                    grid.opened(card.modelData.key)
            }
        }
    }
}
