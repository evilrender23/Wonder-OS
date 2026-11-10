import QtQuick

/// A list of songs: every one of them, an album, a playlist or what a search found.
ListView {
    id: list
    /// "songs", "album", "playlist" or "search" — and what of it (see TrackModel).
    property string mode: "songs"
    property string key
    property string heading
    /// Under the heading of an album: its artist, which leads to their page.
    property string artist
    property string detail
    property string coverPath
    property string coverSymbol: "disc"
    property bool canGoBack: false
    /// An album or a playlist opens with its cover, large.
    readonly property bool hero: mode === "album" || mode === "playlist"
    /// An album numbers its tracks and need not repeat its own name on each.
    readonly property bool numbered: mode === "album"
    readonly property bool wide: width > 760
    readonly property alias tracks: tracks
    signal backRequested()
    signal artistRequested(string name)
    /// `row`: {path, albumKey, artist, slot}.
    signal menuRequested(var row)
    signal headerMenuRequested()

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    bottomMargin: 12
    model: TrackModel {
        id: tracks
        mode: list.mode === "album" ? "album" : list.mode
        key: list.key
    }
    onKeyChanged: {
        currentIndex = -1
        positionViewAtBeginning()
    }
    currentIndex: -1
    highlightFollowsCurrentItem: false

    function play(index) {
        App.player.playTracks(tracks.paths, index)
    }

    header: Item {
        width: list.width
        height: list.hero ? 268 : 96

        TButton {
            x: 20
            y: list.hero ? 14 : 22
            visible: list.canGoBack
            symbol: "chevron-left"
            size: 20
            tone: Theme.tint
            onClicked: list.backRequested()
        }

        // An album or a playlist: its cover and what it is.
        Cover {
            id: art
            visible: list.hero
            x: 32
            y: 46
            width: 196
            height: 196
            corner: 4
            symbol: list.coverSymbol
            path: list.coverPath
        }
        Column {
            visible: list.hero
            anchors.left: art.right
            anchors.leftMargin: 26
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: art.bottom
            spacing: 3
            Text {
                width: parent.width
                text: list.heading
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                width: Math.min(implicitWidth, parent.width)
                visible: text !== ""
                text: App.shown(list.artist)
                color: Theme.tint
                font.pixelSize: 20
                elide: Text.ElideRight
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: list.artistRequested(list.artist)
                }
            }
            Text {
                width: parent.width
                topPadding: 2
                text: [list.detail, App.songs(tracks.count), tracks.count > 0 ? App.span(tracks.duration) : ""].filter(part => part !== "").join(" · ")
                color: Theme.dim
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Item { width: 1; height: 14 }
            Row {
                spacing: 10
                PillButton {
                    symbol: "play-fill"
                    text: qsTr("Reproducir")
                    onClicked: list.play(0)
                }
                PillButton {
                    symbol: "shuffle"
                    text: qsTr("Aleatorio")
                    onClicked: App.player.shuffleTracks(tracks.paths)
                }
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    symbol: "ellipsis"
                    tone: Theme.tint
                    onClicked: list.headerMenuRequested()
                }
            }
        }

        // Every song, or a search: a heading and the names of the columns.
        Text {
            visible: !list.hero
            x: list.canGoBack ? 56 : 32
            y: 18
            width: parent.width - x - 200
            text: list.heading
            color: Theme.text
            font.pixelSize: 26
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Row {
            visible: !list.hero && tracks.count > 0
            anchors.right: parent.right
            anchors.rightMargin: 28
            y: 22
            spacing: 8
            PillButton {
                implicitWidth: 44
                symbol: "play-fill"
                onClicked: list.play(0)
            }
            PillButton {
                implicitWidth: 44
                symbol: "shuffle"
                onClicked: App.player.shuffleTracks(tracks.paths)
            }
        }
        Item {
            visible: !list.hero && tracks.count > 0
            x: 20
            y: 66
            width: parent.width - 40
            height: 28
            Text {
                x: 92
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Canción")
                color: Theme.dim
                font.pixelSize: 11
            }
            Text {
                visible: list.wide
                x: Math.round(parent.width * 0.56)
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Álbum")
                color: Theme.dim
                font.pixelSize: 11
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 38
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Duración")
                color: Theme.dim
                font.pixelSize: 11
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
        Rectangle {
            visible: list.hero
            x: 20
            anchors.bottom: parent.bottom
            width: parent.width - 40
            height: 1
            color: Theme.border
        }
    }

    footer: Item {
        width: list.width
        height: 56
        Text {
            anchors.centerIn: parent
            visible: tracks.count === 0
            text: list.mode === "search" ? qsTr("Ninguna canción coincide.") : (list.mode === "playlist" ? qsTr("Esta playlist está vacía. Añade canciones desde su menú «…».") : "")
            color: Theme.dim
            font.pixelSize: 13
        }
        Text {
            x: 32
            anchors.verticalCenter: parent.verticalCenter
            visible: tracks.count > 0 && !list.hero
            text: App.songs(tracks.count) + " · " + App.span(tracks.duration)
            color: Theme.dim
            font.pixelSize: 12
        }
    }

    delegate: Item {
        id: row
        required property int index
        required property string path
        required property string title
        required property string artist
        required property string album
        required property string albumKey
        required property int number
        required property real duration
        required property int slot

        readonly property bool current: App.player.currentPath === path
        readonly property bool selected: list.currentIndex === index
        readonly property bool hovered: area.containsMouse || more.hovering || love.hovering
        readonly property bool favorite: App.playlists.revision >= 0 && App.playlists.isFavorite(path)

        width: list.width
        height: list.numbered ? 38 : 48

        function menu() {
            list.currentIndex = index
            list.menuRequested({ path: path, albumKey: albumKey, artist: artist, slot: slot })
        }

        Rectangle {
            x: 20
            width: parent.width - 40
            height: parent.height
            radius: 6
            color: row.selected ? Theme.selection : (row.hovered ? Theme.hover : (row.index % 2 ? "transparent" : Theme.stripe))
        }
        MouseArea {
            id: area
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                list.currentIndex = row.index
                if (mouse.button === Qt.RightButton)
                    row.menu()
            }
            onDoubleClicked: mouse => {
                if (mouse.button === Qt.LeftButton)
                    list.play(row.index)
            }
        }

        // The number of the track; the bars if it plays; «play» under the pointer.
        Item {
            id: lead
            x: 20
            width: 44
            height: parent.height
            Text {
                anchors.centerIn: parent
                visible: list.numbered && !row.current && !row.hovered
                text: row.number > 0 ? row.number : "–"
                color: Theme.dim
                font.pixelSize: 13
                font.features: { "tnum": 1 }
            }
            Bars {
                anchors.centerIn: parent
                visible: row.current && !row.hovered
                running: App.player.playing
            }
            Sym {
                anchors.centerIn: parent
                visible: row.hovered
                name: row.current && App.player.playing ? "pause-fill" : "play-fill"
                size: 14
                color: Theme.tint
            }
            MouseArea {
                anchors.fill: parent
                enabled: row.hovered
                onClicked: {
                    if (row.current)
                        App.player.toggle()
                    else
                        list.play(row.index)
                }
            }
        }
        Cover {
            id: art
            visible: !list.numbered
            anchors.left: lead.right
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? 36 : 0
            height: 36
            path: visible ? row.path : ""
        }
        Column {
            anchors.left: art.right
            anchors.leftMargin: list.numbered ? 0 : 12
            anchors.right: list.numbered || !list.wide ? love.left : albumName.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                width: parent.width
                text: row.title
                color: row.current ? Theme.tint : Theme.text
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: !list.numbered
                text: App.shown(row.artist)
                color: Theme.dim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
        Text {
            id: albumName
            visible: !list.numbered && list.wide
            anchors.left: parent.left
            anchors.leftMargin: 20 + Math.round((parent.width - 40) * 0.56)
            anchors.right: love.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: App.shown(row.album)
            color: Theme.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        TButton {
            id: love
            readonly property bool hovering: loveHover.hovered
            anchors.right: time.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            symbol: row.favorite ? "heart-fill" : "heart"
            size: 14
            tone: row.favorite ? Theme.tint : Theme.dim
            opacity: row.favorite || row.hovered ? 1 : 0
            onClicked: App.playlists.toggleFavorite(row.path)
            HoverHandler { id: loveHover }
        }
        Text {
            id: time
            anchors.right: more.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            horizontalAlignment: Text.AlignRight
            text: App.time(row.duration)
            color: Theme.dim
            font.pixelSize: 12
            font.features: { "tnum": 1 }
        }
        TButton {
            id: more
            readonly property bool hovering: moreHover.hovered
            anchors.right: parent.right
            anchors.rightMargin: 26
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            symbol: "ellipsis"
            tone: Theme.tint
            opacity: row.hovered ? 1 : 0
            onClicked: row.menu()
            HoverHandler { id: moreHover }
        }
    }
}
