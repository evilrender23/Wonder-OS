import QtQuick

/// Every artist, as a round portrait made of one of their covers.
GridView {
    id: grid
    signal artistOpened(string name)

    readonly property int pad: 22
    readonly property int columns: Math.max(2, Math.floor((width - 2 * pad) / 168))
    leftMargin: pad
    rightMargin: pad
    bottomMargin: 16
    cellWidth: Math.floor((width - 2 * pad) / columns)
    cellHeight: cellWidth + 44
    clip: true
    model: App.library.artists
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: 600

    header: Item {
        width: grid.width - 2 * grid.pad
        height: 66
        Text {
            x: 10
            y: 18
            text: "Artistas"
            color: Theme.text
            font.pixelSize: 26
            font.weight: Font.Bold
        }
    }

    delegate: Item {
        id: cell
        required property var modelData
        width: grid.cellWidth
        height: grid.cellHeight
        Cover {
            id: art
            x: 16
            y: 6
            width: parent.width - 32
            height: width
            corner: 50
            symbol: "mic"
            path: cell.modelData.cover
            scale: area.pressed ? 0.97 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
        }
        Text {
            id: name
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8
            anchors.top: art.bottom
            anchors.topMargin: 8
            horizontalAlignment: Text.AlignHCenter
            text: cell.modelData.name
            color: Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            anchors.left: name.left
            anchors.right: name.right
            anchors.top: name.bottom
            anchors.topMargin: 1
            horizontalAlignment: Text.AlignHCenter
            text: cell.modelData.albums === 1 ? "1 álbum" : cell.modelData.albums + " álbumes"
            color: Theme.dim
            font.pixelSize: 12
        }
        MouseArea {
            id: area
            anchors.fill: parent
            onClicked: grid.artistOpened(cell.modelData.name)
        }
    }
}
