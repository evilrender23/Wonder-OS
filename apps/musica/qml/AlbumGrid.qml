import QtQuick

/// A wall of covers: the recent albums, all of them, or one artist's.
GridView {
    id: grid
    property var albums: []
    property string heading
    /// Set on an artist's page: adds «Reproducir» and «Aleatorio» for everything of theirs.
    property string artist
    /// Under each cover: the year (an artist's page) instead of the artist.
    property bool showYear: false
    signal albumOpened(string key)
    signal menuRequested(var album)
    signal backRequested()
    property bool canGoBack: false

    readonly property int pad: 22
    readonly property int columns: Math.max(2, Math.floor((width - 2 * pad) / 188))
    leftMargin: pad
    rightMargin: pad
    bottomMargin: 16
    cellWidth: Math.floor((width - 2 * pad) / columns)
    cellHeight: cellWidth + 46
    clip: true
    model: albums
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: 600

    header: Item {
        width: grid.width - 2 * grid.pad
        height: grid.artist !== "" ? 116 : 66
        TButton {
            id: back
            x: 2
            y: 22
            visible: grid.canGoBack
            symbol: "chevron-left"
            size: 20
            tone: Theme.tint
            onClicked: grid.backRequested()
        }
        Text {
            x: grid.canGoBack ? 38 : 10
            y: 18
            width: parent.width - x - 10
            text: grid.heading
            color: Theme.text
            font.pixelSize: 26
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Row {
            x: 10
            y: 66
            spacing: 10
            visible: grid.artist !== ""
            PillButton {
                symbol: "play-fill"
                text: qsTr("Reproducir")
                onClicked: App.player.playTracks(App.library.artistPaths(grid.artist), 0)
            }
            PillButton {
                symbol: "shuffle"
                text: qsTr("Aleatorio")
                onClicked: App.player.shuffleTracks(App.library.artistPaths(grid.artist))
            }
        }
    }

    delegate: Item {
        id: cell
        required property var modelData
        width: grid.cellWidth
        height: grid.cellHeight
        readonly property bool current: App.player.hasTrack && App.player.current.albumKey === modelData.key

        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: 4
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton)
                    grid.menuRequested(cell.modelData)
                else
                    grid.albumOpened(cell.modelData.key)
            }
        }
        Cover {
            id: art
            x: 10
            y: 6
            width: parent.width - 20
            height: width
            corner: 4
            symbol: "disc"
            path: cell.modelData.cover

            Rectangle {
                anchors.fill: parent
                radius: width * 0.04
                color: "#40000000"
                opacity: area.containsMouse || playArea.containsMouse || moreArea.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }

                Rectangle {
                    x: 10
                    y: parent.height - height - 10
                    width: 34
                    height: 34
                    radius: 17
                    color: playArea.containsMouse ? Theme.tint : "#b3303030"
                    Sym {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 1
                        name: "play-fill"
                        size: 15
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: playArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: App.player.playTracks(App.library.albumPaths(cell.modelData.key), 0)
                    }
                }
                Rectangle {
                    x: parent.width - width - 10
                    y: parent.height - height - 10
                    width: 34
                    height: 34
                    radius: 17
                    color: moreArea.containsMouse ? Theme.tint : "#b3303030"
                    Sym {
                        anchors.centerIn: parent
                        name: "ellipsis"
                        size: 16
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: moreArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: grid.menuRequested(cell.modelData)
                    }
                }
            }
        }
        Text {
            id: name
            anchors.left: art.left
            anchors.right: art.right
            anchors.top: art.bottom
            anchors.topMargin: 6
            text: App.shown(cell.modelData.title)
            color: cell.current ? Theme.tint : Theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            anchors.left: art.left
            anchors.right: art.right
            anchors.top: name.bottom
            anchors.topMargin: 1
            text: grid.showYear ? (cell.modelData.year > 0 ? cell.modelData.year : App.songs(cell.modelData.count)) : App.shown(cell.modelData.artist)
            color: Theme.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
