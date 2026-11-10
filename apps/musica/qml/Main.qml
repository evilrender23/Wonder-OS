import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win

    /// What the window shows: {page, key}. Pages: recent, albums, artists, songs,
    /// album (key: its key), artist (its name), playlist (its id), search.
    property var route: ({ page: App.setting("page", "recent"), key: "" })
    property var backStack: []
    property var beforeSearch: null
    property bool queueOpen: App.setting("queueOpen", false)
    readonly property bool typing: activeFocusItem instanceof TextInput
    readonly property bool empty: App.library.count === 0

    width: App.testing ? App.testSize.width : App.setting("width", 1120)
    height: App.testing ? App.testSize.height : App.setting("height", 720)
    minimumWidth: 780
    minimumHeight: 460
    visible: true
    title: App.player.hasTrack ? App.player.current.title + " — " + App.player.current.artist : "Música"
    color: App.glass ? "transparent" : Theme.window

    onClosing: {
        if (App.testing)
            return
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("queueOpen", queueOpen)
        App.setSetting("page", ["recent", "albums", "artists", "songs"].includes(route.page) ? route.page : "recent")
    }
    Component.onCompleted: {
        if (!["recent", "albums", "artists", "songs"].includes(route.page))
            route = { page: "recent", key: "" }
        if (App.testing)
            testStep.start()
    }

    // --- Where the window is ---------------------------------------------

    /// From the sidebar: a fresh start, nothing to go back to.
    function show(page, key) {
        search.text = ""
        beforeSearch = null
        backStack = []
        route = { page: page, key: key ?? "" }
    }
    /// From inside a page: «back» returns here.
    function go(page, key) {
        if (route.page === page && route.key === key)
            return
        const from = route.page === "search" && beforeSearch ? route : route
        backStack = backStack.concat([from])
        route = { page: page, key: key ?? "" }
    }
    function back() {
        if (backStack.length === 0)
            return
        const stack = backStack.slice()
        route = stack.pop()
        backStack = stack
    }
    function openAlbum(key) {
        if (key && App.library.album(key).key !== undefined)
            go("album", key)
    }

    Connections {
        target: App
        function onRaiseRequested() {
            win.show()
            win.raise()
            win.requestActivate()
        }
    }
    Connections {
        target: App.player
        function onFailed(message) {
            toast.say(message)
        }
    }
    Connections {
        target: App.playlists
        function onListChanged() {
            // The playlist on show was deleted.
            if (win.route.page === "playlist" && win.route.key !== "favorites" && App.playlists.name(win.route.key) === "")
                win.show("recent")
        }
    }

    // --- Keys ---------------------------------------------------------------

    Shortcut {
        sequence: "Space"
        enabled: !win.typing
        onActivated: App.player.toggle()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        onActivated: App.player.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        onActivated: App.player.previous()
    }
    Shortcut {
        sequence: "Ctrl+Up"
        onActivated: App.player.volume = App.player.volume + 0.05
    }
    Shortcut {
        sequence: "Ctrl+Down"
        onActivated: App.player.volume = App.player.volume - 0.05
    }
    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: {
            search.forceActiveFocus()
            search.selectAll()
        }
    }
    Shortcut {
        sequences: [StandardKey.Back, "Alt+Left"]
        onActivated: win.back()
    }
    Shortcut {
        sequence: "Ctrl+O"
        onActivated: folderDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+N"
        onActivated: win.newPlaylist([])
    }
    Shortcut {
        sequence: "Ctrl+U"
        onActivated: win.queueOpen = !win.queueOpen
    }
    Shortcut {
        sequences: [StandardKey.Quit, StandardKey.Close]
        onActivated: win.close()
    }
    Shortcut {
        sequence: "Escape"
        enabled: search.activeFocus
        onActivated: {
            search.text = ""
            pages.forceActiveFocus()
        }
    }

    // --- Menus and dialogs --------------------------------------------------

    FolderDialog {
        id: folderDialog
        title: "Añadir carpeta de música"
        onAccepted: App.library.addFolder(selectedFolder)
    }

    property var pendingPaths: []
    property string renaming
    function newPlaylist(paths) {
        pendingPaths = paths
        renaming = ""
        nameDialog.ask("Nueva playlist", "", "Crear")
    }
    NameDialog {
        id: nameDialog
        onAccepted: name => {
            if (win.renaming !== "") {
                App.playlists.rename(win.renaming, name)
            } else {
                const id = App.playlists.create(name, win.pendingPaths)
                if (win.pendingPaths.length === 0)
                    win.show("playlist", id)
                else
                    toast.say("Añadido a «" + name + "».")
            }
        }
    }

    /// The menu of a song, an album or an artist: `paths` is what it acts on.
    FMenu {
        id: songMenu
        property var paths: []
        property string albumKey
        property string artist
        /// Set when the song was picked inside a playlist: where, to take it out.
        property string playlistId
        property int slot: -1
        readonly property bool single: paths.length === 1
        readonly property bool favorite: single && App.playlists.revision >= 0 && App.playlists.isFavorite(paths[0])

        function showFor(what) {
            paths = what.paths
            albumKey = what.albumKey ?? ""
            artist = what.artist ?? ""
            playlistId = what.playlistId ?? ""
            slot = what.slot ?? -1
            popup()
        }

        FMenuItem {
            text: "Reproducir"
            onTriggered: App.player.playTracks(songMenu.paths, 0)
        }
        FMenuItem {
            text: "Reproducir a continuación"
            onTriggered: App.player.playNext(songMenu.paths)
        }
        FMenuItem {
            text: "Añadir a la cola"
            onTriggered: App.player.addToQueue(songMenu.paths)
        }
        FSeparator {}
        FMenu {
            id: addMenu
            title: "Añadir a una playlist"
            FMenuItem {
                text: "Nueva playlist…"
                onTriggered: win.newPlaylist(songMenu.paths)
            }
            FSeparator {
                visible: App.playlists.list.length > 0
            }
            Instantiator {
                model: App.playlists.list
                delegate: FMenuItem {
                    required property var modelData
                    text: modelData.name
                    onTriggered: {
                        App.playlists.add(modelData.id, songMenu.paths)
                        toast.say("Añadido a «" + modelData.name + "».")
                    }
                }
                onObjectAdded: (index, object) => addMenu.insertItem(index + 2, object)
                onObjectRemoved: (index, object) => addMenu.removeItem(object)
            }
        }
        FMenuItem {
            visible: songMenu.single
            text: songMenu.favorite ? "Quitar de Favoritas" : "Añadir a Favoritas"
            onTriggered: App.playlists.toggleFavorite(songMenu.paths[0])
        }
        FMenuItem {
            visible: !songMenu.single
            text: "Añadir a Favoritas"
            onTriggered: App.playlists.add("favorites", songMenu.paths)
        }
        FSeparator {}
        FMenuItem {
            visible: songMenu.albumKey !== "" && !(win.route.page === "album" && win.route.key === songMenu.albumKey)
            text: "Ir al álbum"
            onTriggered: win.openAlbum(songMenu.albumKey)
        }
        FMenuItem {
            visible: songMenu.artist !== "" && !(win.route.page === "artist" && win.route.key === songMenu.artist)
            text: "Ir al artista"
            onTriggered: win.go("artist", songMenu.artist)
        }
        FMenuItem {
            visible: songMenu.single
            text: "Mostrar en el Finder"
            onTriggered: App.reveal(songMenu.paths[0])
        }
        FSeparator {
            visible: songMenu.slot >= 0
        }
        FMenuItem {
            visible: songMenu.slot >= 0
            text: songMenu.playlistId === "favorites" ? "Quitar de Favoritas" : "Quitar de la playlist"
            onTriggered: App.playlists.removeAt(songMenu.playlistId, songMenu.slot)
        }
    }

    FMenu {
        id: playlistMenu
        property string playlistId
        function showFor(id) {
            playlistId = id
            popup()
        }
        FMenuItem {
            text: "Reproducir"
            onTriggered: App.player.playTracks(App.playlists.paths(playlistMenu.playlistId), 0)
        }
        FMenuItem {
            text: "Añadir a la cola"
            onTriggered: App.player.addToQueue(App.playlists.paths(playlistMenu.playlistId))
        }
        FSeparator {
            visible: playlistMenu.playlistId !== "favorites"
        }
        FMenuItem {
            visible: playlistMenu.playlistId !== "favorites"
            text: "Cambiar nombre…"
            onTriggered: {
                win.renaming = playlistMenu.playlistId
                nameDialog.ask("Cambiar nombre", App.playlists.name(playlistMenu.playlistId), "Guardar")
            }
        }
        FMenuItem {
            visible: playlistMenu.playlistId !== "favorites"
            text: "Eliminar playlist"
            onTriggered: App.playlists.remove(playlistMenu.playlistId)
        }
    }

    FMenu {
        id: libraryMenu
        FMenuItem {
            text: "Añadir carpeta de música…"
            onTriggered: folderDialog.open()
        }
        FMenuItem {
            text: "Actualizar biblioteca"
            enabled: !App.library.scanning
            onTriggered: App.library.rescan()
        }
        FSeparator {
            visible: App.library.folders.length > 0
        }
        Instantiator {
            model: App.library.folders
            delegate: FMenuItem {
                required property string modelData
                text: "Quitar «" + modelData.split("/").pop() + "»"
                onTriggered: App.library.removeFolder(modelData)
            }
            onObjectAdded: (index, object) => libraryMenu.insertItem(index + 3, object)
            onObjectRemoved: (index, object) => libraryMenu.removeItem(object)
        }
    }

    // --- The window ----------------------------------------------------------

    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (drop.hasUrls) {
                App.open(drop.urls)
                drop.accept()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            Layout.fillHeight: true
            implicitWidth: 216
            color: Theme.sidebar

            TextField {
                id: search
                x: 12
                y: 14
                width: parent.width - 24
                placeholderText: "Buscar"
                placeholderTextColor: Theme.dim
                color: Theme.text
                font.pixelSize: 13
                selectionColor: Theme.tint
                selectedTextColor: "#ffffff"
                leftPadding: 28
                rightPadding: 24
                background: Rectangle {
                    implicitHeight: 28
                    radius: 7
                    color: Theme.field
                    border.color: search.activeFocus ? Theme.tint : "transparent"
                    Sym {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: 13
                        color: Theme.dim
                    }
                }
                onTextChanged: {
                    const query = text.trim()
                    if (query !== "" && win.route.page !== "search") {
                        win.beforeSearch = { route: win.route, stack: win.backStack }
                        win.backStack = []
                        win.route = { page: "search", key: "" }
                    } else if (query === "" && win.route.page === "search" && win.beforeSearch) {
                        win.route = win.beforeSearch.route
                        win.backStack = win.beforeSearch.stack
                        win.beforeSearch = null
                    }
                }
                TButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    visible: search.text !== ""
                    symbol: "x"
                    size: 12
                    tone: Theme.dim
                    onClicked: search.text = ""
                }
            }

            Flickable {
                anchors.top: search.bottom
                anchors.topMargin: 12
                anchors.bottom: sideFoot.top
                x: 10
                width: parent.width - 20
                contentHeight: entries.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: entries
                    width: parent.width
                    spacing: 1

                    Text {
                        leftPadding: 9
                        bottomPadding: 4
                        text: "Biblioteca"
                        color: Theme.dim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: [
                            { page: "recent", symbol: "clock", text: "Añadido recientemente" },
                            { page: "artists", symbol: "mic", text: "Artistas" },
                            { page: "albums", symbol: "disc", text: "Álbumes" },
                            { page: "songs", symbol: "music", text: "Canciones" },
                        ]
                        SideItem {
                            required property var modelData
                            symbol: modelData.symbol
                            text: modelData.text
                            selected: win.route.page === modelData.page
                            onClicked: win.show(modelData.page)
                        }
                    }

                    Text {
                        leftPadding: 9
                        topPadding: 16
                        bottomPadding: 4
                        text: "Playlists"
                        color: Theme.dim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    SideItem {
                        symbol: "heart"
                        text: "Favoritas"
                        selected: win.route.page === "playlist" && win.route.key === "favorites"
                        onClicked: win.show("playlist", "favorites")
                        onMenuRequested: playlistMenu.showFor("favorites")
                    }
                    Repeater {
                        model: App.playlists.list
                        SideItem {
                            required property var modelData
                            symbol: "list-music"
                            text: modelData.name
                            selected: win.route.page === "playlist" && win.route.key === modelData.id
                            onClicked: win.show("playlist", modelData.id)
                            onMenuRequested: playlistMenu.showFor(modelData.id)
                        }
                    }
                    SideItem {
                        symbol: "plus"
                        text: "Nueva playlist"
                        tone: Theme.dim
                        onClicked: win.newPlaylist([])
                    }
                }
            }

            Item {
                id: sideFoot
                anchors.bottom: parent.bottom
                width: parent.width
                height: 40
                Text {
                    x: 19
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 60
                    text: App.library.scanning ? "Actualizando biblioteca…" : App.songs(App.library.count)
                    color: Theme.dim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                TButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "folder-plus"
                    tone: Theme.dim
                    onClicked: libraryMenu.popup()
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
                opacity: App.glass ? 0.5 : 1
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            PlayerBar {
                Layout.fillWidth: true
                queueOpen: win.queueOpen
                onQueueToggled: win.queueOpen = !win.queueOpen
                onAlbumRequested: key => win.openAlbum(key)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.window

                    Loader {
                        id: pages
                        anchors.fill: parent
                        focus: true
                        visible: !win.empty || win.route.page === "playlist"
                        sourceComponent: {
                            switch (win.route.page) {
                            case "artists":
                                return artistsPage
                            case "artist":
                                return artistPage
                            case "songs":
                            case "search":
                                return songsPage
                            case "album":
                                return albumPage
                            case "playlist":
                                return playlistPage
                            }
                            return albumsPage
                        }
                    }

                    // No music yet.
                    Column {
                        anchors.centerIn: parent
                        visible: !pages.visible
                        spacing: 10
                        Sym {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: "music"
                            size: 56
                            color: Theme.faint
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            topPadding: 6
                            text: App.library.scanning ? "Buscando música…" : "Tu música aparecerá aquí"
                            color: Theme.text
                            font.pixelSize: 20
                            font.weight: Font.Bold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 380
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: App.library.folders.length > 0
                                ? "No hay canciones en " + App.library.folders.map(f => "«" + f.split("/").pop() + "»").join(", ") + ". Copia música ahí, arrastra archivos a esta ventana o añade otra carpeta."
                                : "Añade una carpeta con tu música, o arrastra canciones a esta ventana."
                            color: Theme.dim
                            font.pixelSize: 13
                        }
                        Item { width: 1; height: 6 }
                        PillButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            symbol: "folder-plus"
                            text: "Añadir carpeta…"
                            onClicked: folderDialog.open()
                        }
                    }

                    // A word about what just happened.
                    Rectangle {
                        id: toast
                        function say(message) {
                            toastText.text = message
                            opacity = 1
                            toastTimer.restart()
                        }
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 18
                        width: Math.min(parent.width - 40, toastText.implicitWidth + 32)
                        height: 34
                        radius: 17
                        color: Theme.dark ? "#f0404044" : "#f02c2c2e"
                        opacity: 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Text {
                            id: toastText
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 24)
                            color: "#ffffff"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Timer {
                            id: toastTimer
                            interval: 3200
                            onTriggered: toast.opacity = 0
                        }
                    }
                }

                QueuePanel {
                    Layout.fillHeight: true
                    implicitWidth: 290
                    visible: win.queueOpen
                }
            }
        }
    }

    // --- Pages ----------------------------------------------------------------

    Component {
        id: albumsPage
        AlbumGrid {
            heading: win.route.page === "recent" ? "Añadido recientemente" : "Álbumes"
            albums: win.route.page === "recent" ? App.library.recentAlbums : App.library.albums
            onAlbumOpened: key => win.go("album", key)
            onMenuRequested: album => songMenu.showFor({ paths: App.library.albumPaths(album.key), albumKey: album.key, artist: album.artist })
        }
    }
    Component {
        id: artistsPage
        ArtistGrid {
            onArtistOpened: name => win.go("artist", name)
        }
    }
    Component {
        id: artistPage
        AlbumGrid {
            heading: win.route.key
            artist: win.route.key
            showYear: true
            canGoBack: win.backStack.length > 0
            albums: App.library.count >= 0 ? App.library.albumsOf(win.route.key) : []
            onBackRequested: win.back()
            onAlbumOpened: key => win.go("album", key)
            onMenuRequested: album => songMenu.showFor({ paths: App.library.albumPaths(album.key), albumKey: album.key })
        }
    }
    Component {
        id: songsPage
        TrackList {
            readonly property string query: search.text.trim()
            mode: win.route.page === "search" ? "search" : "songs"
            key: mode === "search" ? query : ""
            heading: mode === "search" ? "Resultados de «" + query + "»" : "Canciones"
            onMenuRequested: row => songMenu.showFor({ paths: [row.path], albumKey: row.albumKey, artist: row.artist })
        }
    }
    Component {
        id: albumPage
        TrackList {
            readonly property var album: App.library.count >= 0 ? App.library.album(win.route.key) : ({})
            mode: "album"
            key: win.route.key
            heading: album.title ?? ""
            artist: album.artist ?? ""
            detail: [album.genre ?? "", album.year > 0 ? String(album.year) : ""].filter(part => part !== "").join(" · ")
            coverPath: album.cover ?? ""
            canGoBack: win.backStack.length > 0
            onBackRequested: win.back()
            onArtistRequested: name => win.go("artist", name)
            onMenuRequested: row => songMenu.showFor({ paths: [row.path], albumKey: row.albumKey, artist: row.artist })
            onHeaderMenuRequested: songMenu.showFor({ paths: tracks.paths, artist: artist })
        }
    }
    Component {
        id: playlistPage
        TrackList {
            readonly property bool favorites: win.route.key === "favorites"
            mode: "playlist"
            key: win.route.key
            heading: App.playlists.list.length >= 0 ? App.playlists.name(win.route.key) : ""
            detail: "Playlist"
            coverPath: tracks.paths.length > 0 ? tracks.paths[0] : ""
            coverSymbol: favorites ? "heart" : "list-music"
            canGoBack: win.backStack.length > 0
            onBackRequested: win.back()
            onMenuRequested: row => songMenu.showFor({ paths: [row.path], albumKey: row.albumKey, artist: row.artist, playlistId: win.route.key, slot: row.slot })
            onHeaderMenuRequested: playlistMenu.showFor(win.route.key)
        }
    }

    // --- Pictures for the tests (--shot, --do) -----------------------------------

    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        const library = App.library
        switch (verb) {
        case "page":
            show(arg)
            break
        case "album":
            go("album", library.albums[Number(arg)].key)
            break
        case "artist":
            go("artist", library.artists[Number(arg)].name)
            break
        case "play":
            App.player.playTracks(library.albumPaths(library.albums[Number(arg)].key), 0)
            App.player.seek(42000)
            break
        case "queue":
            queueOpen = true
            break
        case "search":
            search.text = arg
            break
        case "favorite":
            App.playlists.toggleFavorite(library.albumPaths(library.albums[0].key)[Number(arg)])
            break
        case "playlist":
            show("playlist", App.playlists.create(arg, library.albumPaths(library.albums[0].key)))
            break
        case "menu":
            songMenu.showFor({ paths: [library.albumPaths(library.albums[0].key)[0]], albumKey: library.albums[0].key, artist: library.albums[0].artist })
            break
        case "name":
            newPlaylist([])
            break
        }
    }
    Timer {
        id: testStep
        // The library is read in the background: give it time to arrive.
        interval: 1200
        onTriggered: {
            for (const action of App.testActions)
                win.runAction(action.trim())
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 1500
        onTriggered: App.saveShot(win)
    }
}
