import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win

    /// What the window shows: {page, key}. Pages of pictures: library, favorites,
    /// videos, screenshots, folder (key: its path), album (key: its id).
    /// Pages of cards: folders, albums.
    property var route: ({ page: "library", key: "" })
    property var backStack: []
    readonly property bool cards: route.page === "folders" || route.page === "albums"
    readonly property bool typing: activeFocusItem instanceof TextInput
    readonly property bool empty: App.library.count === 0
    /// year, month, day or none.
    property string grouping: App.setting("grouping", "month")
    readonly property var sizes: [96, 124, 150, 190, 240, 320]
    property int sizeStep: Math.max(0, Math.min(sizes.length - 1, App.setting("sizeStep", 2)))
    /// Goes up when a file has new contents: pictures addressed with it are read again.
    property int epoch: 0

    width: App.testing ? App.testSize.width : App.setting("width", 1120)
    height: App.testing ? App.testSize.height : App.setting("height", 720)
    minimumWidth: 720
    minimumHeight: 440
    visible: true
    title: viewer.visible && viewer.item.name ? qsTr("%1 — Fotos").arg(viewer.item.name) : qsTr("Fotos")
    color: App.glass ? "transparent" : Theme.window

    onClosing: {
        if (App.testing)
            return
        if (visibility === Window.Windowed) {
            App.setSetting("width", width)
            App.setSetting("height", height)
        }
        App.setSetting("grouping", grouping)
        App.setSetting("sizeStep", sizeStep)
        App.setSetting("page", ["library", "favorites", "videos", "screenshots", "folders", "albums"].includes(route.page) ? route.page : "library")
    }
    Component.onCompleted: {
        const page = App.setting("page", "library")
        if (["favorites", "videos", "screenshots", "folders", "albums"].includes(page))
            route = { page: page, key: "" }
        if (App.testing)
            testStep.start()
    }

    // --- Where the window is ---------------------------------------------

    /// From the sidebar: a fresh start, nothing to go back to.
    function visit(page, key) {
        clearSelection()
        backStack = []
        route = { page: page, key: key ?? "" }
    }
    /// From inside a page: «back» returns here.
    function go(page, key) {
        clearSelection()
        backStack = backStack.concat([route])
        route = { page: page, key: key ?? "" }
    }
    function back() {
        if (backStack.length === 0)
            return
        clearSelection()
        const stack = backStack.slice()
        route = stack.pop()
        backStack = stack
    }
    readonly property string heading: {
        switch (route.page) {
        case "favorites": return qsTr("Favoritos")
        case "videos": return qsTr("Vídeos")
        case "screenshots": return qsTr("Capturas de pantalla")
        case "folders": return qsTr("Carpetas")
        case "albums": return qsTr("Álbumes")
        case "folder": return App.library.revision >= 0 ? App.library.placeName(route.key) : ""
        case "album": return App.albums.revision >= 0 ? App.albums.name(route.key) : ""
        }
        return qsTr("Fototeca")
    }
    readonly property var folderCards: App.library.places.map(place => ({ key: place.path, name: place.name, count: place.count, cover: place.cover, stamp: place.coverStamp }))
    readonly property var albumCards: App.albums.list.map(album => ({ key: album.id, name: album.name, count: album.count, cover: album.cover, stamp: "a" + epoch }))

    // --- What is chosen -----------------------------------------------------

    property var selection: ({})
    property int anchor: -1
    readonly property var selectedPaths: grid.photos.paths.filter(path => selection[path] === true)

    function clearSelection() {
        selection = ({})
        anchor = -1
    }
    function choose(paths, on) {
        const next = Object.assign({}, selection)
        for (const path of paths) {
            if (on)
                next[path] = true
            else
                delete next[path]
        }
        selection = next
    }
    function pick(index, path, modifiers) {
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            choose(grid.photos.range(anchor, index), true)
        } else {
            choose([path], selection[path] !== true)
            anchor = index
        }
    }
    /// What an action applies to: the chosen ones when this is one of them, else this alone.
    function targets(path) {
        return selection[path] === true ? selectedPaths : [path]
    }

    // --- What is done to pictures ---------------------------------------------

    function trash(paths) {
        if (paths.length === 0)
            return
        const one = paths.length === 1
        confirm.ask(one ? qsTr("¿Mover «%1» a la papelera?").arg(paths[0].split("/").pop()) : qsTr("¿Mover %1 elementos a la papelera?").arg(paths.length),
                    one ? qsTr("Se quitará de la biblioteca y de tus álbumes. Podrás recuperarlo desde la papelera.") : qsTr("Se quitarán de la biblioteca y de tus álbumes. Podrás recuperarlos desde la papelera."), qsTr("Mover a la papelera"), () => {
            const gone = App.trash(paths)
            if (gone > 0) {
                viewer.dropped(paths)
                choose(paths, false)
                toast.say(gone === 1 ? qsTr("Movido a la papelera.") : qsTr("%1 elementos movidos a la papelera.").arg(gone))
            }
        })
    }
    function copy(paths) {
        App.copy(paths)
        toast.say(paths.length === 1 ? qsTr("Copiado.") : qsTr("%1 elementos copiados.").arg(paths.length))
    }
    function addTo(id, paths) {
        const added = App.albums.add(id, paths)
        const name = App.albums.name(id)
        toast.say(added === 0 ? qsTr("Ya estaba en «%1».").arg(name) : (added === 1 ? qsTr("Añadido a «%1».").arg(name) : qsTr("%1 elementos añadidos a «%2».").arg(added).arg(name)))
    }
    property var pendingPaths: []
    property string renaming
    function newAlbum(paths) {
        pendingPaths = paths
        renaming = ""
        nameDialog.ask(qsTr("Nuevo álbum"), "", qsTr("Crear"))
    }
    function renameAlbum(id) {
        renaming = id
        nameDialog.ask(qsTr("Renombrar álbum"), App.albums.name(id), qsTr("Renombrar"))
    }
    function deleteAlbum(id) {
        confirm.ask(qsTr("¿Eliminar el álbum «%1»?").arg(App.albums.name(id)), qsTr("Sus fotos y vídeos siguen en la biblioteca."), qsTr("Eliminar"), () => App.albums.remove(id))
    }
    function openAt(index) {
        viewer.show(grid.photos.paths, index)
    }

    Connections {
        target: App
        function onRaiseRequested() {
            win.raise()
            win.requestActivate()
        }
        function onShowRequested(paths, index) {
            viewer.show(paths, index)
        }
        function onFailed(message) {
            toast.say(message)
        }
    }
    Connections {
        target: App.library
        function onItemChanged(path) {
            win.epoch += 1
        }
    }
    Connections {
        target: App.albums
        function onChanged() {
            // The album on show was deleted.
            if (win.route.page === "album" && App.albums.name(win.route.key) === "")
                win.visit("albums")
        }
    }

    // --- Keys ---------------------------------------------------------------

    Shortcut {
        sequences: [StandardKey.Find]
        enabled: !viewer.visible
        onActivated: {
            search.forceActiveFocus()
            search.selectAll()
        }
    }
    Shortcut {
        sequences: [StandardKey.Back, "Alt+Left"]
        enabled: !viewer.visible
        onActivated: win.back()
    }
    Shortcut {
        sequence: "Ctrl+O"
        onActivated: folderDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: !viewer.visible
        onActivated: win.newAlbum(win.selectedPaths)
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: !viewer.visible && !win.typing && !win.cards
        onActivated: win.choose(grid.photos.paths, true)
    }
    Shortcut {
        sequence: "Escape"
        enabled: !viewer.visible && !confirm.visible && !nameDialog.visible
        onActivated: {
            if (win.typing) {
                search.text = ""
                grid.forceActiveFocus()
            } else {
                win.clearSelection()
            }
        }
    }
    Shortcut {
        sequence: "Delete"
        enabled: !viewer.visible && !win.typing && win.selectedPaths.length > 0
        onActivated: win.trash(win.selectedPaths)
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: !viewer.visible && !win.typing && win.selectedPaths.length > 0
        onActivated: win.copy(win.selectedPaths)
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !viewer.visible && !win.typing && !confirm.visible && !nameDialog.visible && win.selectedPaths.length > 0
        onActivated: win.openAt(grid.photos.indexOf(win.selectedPaths[0]))
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: !viewer.visible
        onActivated: win.sizeStep = Math.min(win.sizes.length - 1, win.sizeStep + 1)
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        enabled: !viewer.visible
        onActivated: win.sizeStep = Math.max(0, win.sizeStep - 1)
    }
    Shortcut {
        sequence: "F5"
        onActivated: App.library.rescan()
    }
    Shortcut {
        sequences: [StandardKey.Quit, "Ctrl+W"]
        onActivated: win.close()
    }

    // --- Dialogs and menus ----------------------------------------------------

    FolderDialog {
        id: folderDialog
        title: qsTr("Añadir carpeta a la biblioteca")
        onAccepted: App.library.addFolder(selectedFolder)
    }
    NameDialog {
        id: nameDialog
        onAccepted: name => {
            if (win.renaming !== "") {
                App.albums.rename(win.renaming, name)
            } else {
                const paths = win.pendingPaths
                const id = App.albums.create(name, paths)
                if (paths.length > 0)
                    toast.say(paths.length === 1 ? qsTr("Añadido a «%1».").arg(name) : qsTr("%1 elementos añadidos a «%2».").arg(paths.length).arg(name))
                else
                    win.visit("album", id)
            }
            win.pendingPaths = []
        }
    }
    ConfirmDialog {
        id: confirm
    }

    FMenu {
        id: photoMenu
        property var paths: []
        readonly property bool single: paths.length === 1
        readonly property bool loved: paths.length > 0 && App.albums.revision >= 0 && App.albums.allFavorite(paths)
        function showFor(list) {
            paths = list
            popup()
        }
        FMenuItem {
            text: qsTr("Abrir")
            visible: photoMenu.single && !viewer.visible
            onTriggered: win.openAt(grid.photos.indexOf(photoMenu.paths[0]))
        }
        FMenuItem {
            text: photoMenu.loved ? qsTr("Quitar de favoritos") : qsTr("Añadir a favoritos")
            onTriggered: App.albums.setFavorite(photoMenu.paths, !photoMenu.loved)
        }
        FMenu {
            id: addMenu
            title: qsTr("Añadir al álbum")
            FMenuItem {
                text: qsTr("Nuevo álbum…")
                onTriggered: win.newAlbum(photoMenu.paths)
            }
            FSeparator {
                visible: App.albums.list.length > 0
            }
            Instantiator {
                model: App.albums.list
                delegate: FMenuItem {
                    required property var modelData
                    text: modelData.name
                    onTriggered: win.addTo(modelData.id, photoMenu.paths)
                }
                onObjectAdded: (index, object) => addMenu.insertItem(index + 2, object)
                onObjectRemoved: (index, object) => addMenu.removeItem(object)
            }
        }
        FMenuItem {
            text: qsTr("Quitar de este álbum")
            visible: win.route.page === "album" && !viewer.visible
            onTriggered: {
                App.albums.take(win.route.key, photoMenu.paths)
                win.choose(photoMenu.paths, false)
            }
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Girar a la derecha")
            visible: photoMenu.single && win.epoch >= 0 && App.canRotate(photoMenu.paths[0] ?? "")
            onTriggered: App.rotate(photoMenu.paths[0], true)
        }
        FMenuItem {
            text: qsTr("Girar a la izquierda")
            visible: photoMenu.single && win.epoch >= 0 && App.canRotate(photoMenu.paths[0] ?? "")
            onTriggered: App.rotate(photoMenu.paths[0], false)
        }
        FMenuItem {
            text: qsTr("Copiar")
            onTriggered: win.copy(photoMenu.paths)
        }
        FMenuItem {
            text: qsTr("Mostrar en Archivos")
            visible: photoMenu.single
            onTriggered: App.reveal(photoMenu.paths[0])
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Mover a la papelera")
            onTriggered: win.trash(photoMenu.paths)
        }
    }
    FMenu {
        id: albumMenu
        property string albumId
        function showFor(id) {
            albumId = id
            popup()
        }
        FMenuItem {
            text: qsTr("Renombrar…")
            onTriggered: win.renameAlbum(albumMenu.albumId)
        }
        FMenuItem {
            text: qsTr("Eliminar álbum")
            onTriggered: win.deleteAlbum(albumMenu.albumId)
        }
    }
    FMenu {
        id: folderMenu
        property string folder
        function showFor(path) {
            folder = path
            popup()
        }
        FMenuItem {
            text: qsTr("Mostrar en Archivos")
            onTriggered: App.reveal(folderMenu.folder)
        }
    }
    FMenu {
        id: libraryMenu
        FMenuItem {
            text: qsTr("Añadir carpeta…")
            onTriggered: folderDialog.open()
        }
        FMenuItem {
            text: qsTr("Volver a leer la biblioteca")
            onTriggered: App.library.rescan()
        }
        FSeparator {
            visible: App.library.folders.length > 0
        }
        Instantiator {
            model: App.library.folders
            delegate: FMenuItem {
                required property string modelData
                text: qsTr("Quitar «%1»").arg(modelData.split("/").pop())
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
            if (drop.hasUrls)
                App.open(drop.urls)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- The sidebar --------------------------------------------------------
        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 212
            color: Theme.sidebar
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
                opacity: 0.6
            }

            TextField {
                id: search
                x: 12
                y: 12
                width: parent.width - 24
                placeholderText: qsTr("Buscar")
                placeholderTextColor: Theme.faint
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
                    // Words look in pictures: a page of cards gives way to the whole library.
                    if (text !== "" && win.cards)
                        win.visit("library")
                }
                TButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: search.text !== ""
                    width: 22
                    height: 22
                    symbol: "x"
                    size: 12
                    tone: Theme.dim
                    onClicked: search.text = ""
                }
            }

            Flickable {
                anchors.top: search.bottom
                anchors.topMargin: 10
                anchors.bottom: sideFoot.top
                x: 8
                width: parent.width - 16
                contentHeight: sideColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: sideColumn
                    width: parent.width
                    spacing: 1

                    component Label: Text {
                        leftPadding: 9
                        topPadding: 12
                        bottomPadding: 4
                        color: Theme.dim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: qsTr("Biblioteca")
                        topPadding: 2
                    }
                    SideItem {
                        symbol: "images"
                        text: qsTr("Fototeca")
                        selected: win.route.page === "library"
                        onClicked: win.visit("library")
                        onMenuRequested: libraryMenu.popup()
                    }
                    SideItem {
                        symbol: "heart"
                        text: qsTr("Favoritos")
                        selected: win.route.page === "favorites"
                        onClicked: win.visit("favorites")
                    }
                    SideItem {
                        symbol: "film"
                        text: qsTr("Vídeos")
                        selected: win.route.page === "videos"
                        onClicked: win.visit("videos")
                    }
                    SideItem {
                        symbol: "monitor"
                        text: qsTr("Capturas de pantalla")
                        selected: win.route.page === "screenshots"
                        onClicked: win.visit("screenshots")
                    }
                    SideItem {
                        symbol: "folder"
                        text: qsTr("Carpetas")
                        selected: win.route.page === "folders" || win.route.page === "folder"
                        onClicked: win.visit("folders")
                        onMenuRequested: libraryMenu.popup()
                    }

                    Item {
                        width: parent.width
                        height: albumsLabel.height
                        Label {
                            id: albumsLabel
                            text: qsTr("Álbumes")
                        }
                        TButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 1
                            width: 22
                            height: 20
                            symbol: "plus"
                            size: 13
                            tone: Theme.dim
                            onClicked: win.newAlbum([])
                        }
                    }
                    SideItem {
                        symbol: "images"
                        text: qsTr("Todos los álbumes")
                        selected: win.route.page === "albums"
                        onClicked: win.visit("albums")
                    }
                    Repeater {
                        model: App.albums.list
                        delegate: SideItem {
                            required property var modelData
                            symbol: "image"
                            text: modelData.name
                            selected: win.route.page === "album" && win.route.key === modelData.id
                            onClicked: win.visit("album", modelData.id)
                            onMenuRequested: albumMenu.showFor(modelData.id)
                        }
                    }
                }
            }

            Item {
                id: sideFoot
                anchors.bottom: parent.bottom
                width: parent.width
                height: 34
                Text {
                    x: 17
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 60
                    text: App.library.scanning ? qsTr("Leyendo la biblioteca…") : App.counted(App.library.photoCount, App.library.videoCount)
                    visible: App.library.scanning || !win.empty
                    color: Theme.dim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                TButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "ellipsis"
                    tone: Theme.dim
                    onClicked: libraryMenu.popup()
                }
            }
        }

        // --- The page -------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.window

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // What this page is, and what can be done with it.
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 52
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: win.backStack.length > 0 ? 8 : 16
                        anchors.rightMargin: 12
                        spacing: 4
                        TButton {
                            visible: win.backStack.length > 0
                            symbol: "chevron-left"
                            size: 18
                            onClicked: win.back()
                        }
                        Text {
                            Layout.maximumWidth: 360
                            text: win.heading
                            color: Theme.text
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.leftMargin: 8
                            Layout.topMargin: 5
                            Layout.fillWidth: true
                            text: {
                                if (win.selectedPaths.length > 0)
                                    return win.selectedPaths.length === 1 ? qsTr("1 seleccionado") : qsTr("%1 seleccionados").arg(win.selectedPaths.length)
                                if (win.route.page === "folders")
                                    return win.folderCards.length === 1 ? qsTr("1 carpeta") : qsTr("%1 carpetas").arg(win.folderCards.length)
                                if (win.route.page === "albums")
                                    return win.albumCards.length === 1 ? qsTr("1 álbum") : qsTr("%1 álbumes").arg(win.albumCards.length)
                                return grid.photos.count > 0 ? App.counted(grid.photos.photoCount, grid.photos.videoCount) : ""
                            }
                            color: win.selectedPaths.length > 0 ? Theme.tint : Theme.dim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        // With pictures chosen: what to do with them.
                        Row {
                            visible: win.selectedPaths.length > 0
                            spacing: 2
                            TButton {
                                readonly property bool loved: App.albums.revision >= 0 && App.albums.allFavorite(win.selectedPaths)
                                symbol: loved ? "heart-fill" : "heart"
                                tone: loved ? Theme.love : Theme.text
                                onClicked: App.albums.setFavorite(win.selectedPaths, !loved)
                            }
                            TButton {
                                symbol: "folder-plus"
                                onClicked: {
                                    photoMenu.paths = win.selectedPaths
                                    addMenu.popup()
                                }
                            }
                            TButton {
                                symbol: "copy"
                                onClicked: win.copy(win.selectedPaths)
                            }
                            TButton {
                                symbol: "trash-2"
                                onClicked: win.trash(win.selectedPaths)
                            }
                            TButton {
                                symbol: "x"
                                tone: Theme.dim
                                onClicked: win.clearSelection()
                            }
                        }

                        // Otherwise: how the page is laid out.
                        Rectangle {
                            visible: win.selectedPaths.length === 0 && !win.cards && win.route.page !== "album" && grid.photos.count > 0
                            implicitWidth: groupRow.width + 4
                            implicitHeight: 26
                            radius: 7
                            color: Theme.field
                            Row {
                                id: groupRow
                                x: 2
                                y: 2
                                Repeater {
                                    model: [{ key: "year", text: qsTr("Años") }, { key: "month", text: qsTr("Meses") }, { key: "day", text: qsTr("Días") }, { key: "none", text: qsTr("Todo") }]
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property bool on: win.grouping === modelData.key
                                        width: label.implicitWidth + 20
                                        height: 22
                                        radius: 5
                                        color: on ? (Theme.dark ? "#55ffffff" : "#ffffff") : "transparent"
                                        Text {
                                            id: label
                                            anchors.centerIn: parent
                                            text: modelData.text
                                            color: Theme.text
                                            font.pixelSize: 12
                                            font.weight: parent.on ? Font.DemiBold : Font.Normal
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: win.grouping = modelData.key
                                        }
                                    }
                                }
                            }
                        }
                        Row {
                            visible: win.selectedPaths.length === 0 && !win.cards && grid.photos.count > 0
                            Layout.leftMargin: 6
                            TButton {
                                symbol: "zoom-out"
                                active: win.sizeStep > 0
                                onClicked: win.sizeStep -= 1
                            }
                            TButton {
                                symbol: "zoom-in"
                                active: win.sizeStep < win.sizes.length - 1
                                onClicked: win.sizeStep += 1
                            }
                        }
                        TButton {
                            visible: win.route.page === "album" && win.selectedPaths.length === 0
                            symbol: "ellipsis"
                            onClicked: albumMenu.showFor(win.route.key)
                        }
                        TButton {
                            visible: win.route.page === "albums"
                            symbol: "plus"
                            onClicked: win.newAlbum([])
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    PhotoGrid {
                        id: grid
                        anchors.fill: parent
                        visible: !win.cards
                        target: win.sizes[win.sizeStep]
                        selection: win.selection
                        photos.mode: win.cards ? "library" : win.route.page
                        photos.key: win.route.key
                        photos.query: search.text
                        photos.grouping: win.grouping
                        onOpened: index => win.openAt(index)
                        onPicked: (index, path, modifiers) => win.pick(index, path, modifiers)
                        onMenuRequested: (index, path) => photoMenu.showFor(win.targets(path))
                        onGroupPicked: paths => win.choose(paths, !paths.every(path => win.selection[path] === true))
                        onBackgroundClicked: win.clearSelection()
                    }
                    CardGrid {
                        anchors.fill: parent
                        visible: win.cards
                        cards: win.route.page === "folders" ? win.folderCards : (win.route.page === "albums" ? win.albumCards : [])
                        emptySymbol: win.route.page === "folders" ? "folder" : "images"
                        onOpened: key => win.go(win.route.page === "folders" ? "folder" : "album", key)
                        onMenuRequested: key => {
                            if (win.route.page === "folders")
                                folderMenu.showFor(key)
                            else
                                albumMenu.showFor(key)
                        }
                    }

                    // Nothing to show: say why, and what to do about it.
                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 60, 380)
                        spacing: 10
                        visible: win.cards ? (win.route.page === "folders" ? win.folderCards.length === 0 : win.albumCards.length === 0) : grid.photos.count === 0
                        readonly property bool reading: win.empty && App.library.scanning
                        readonly property bool wantsFolder: win.empty && !App.library.scanning && win.route.page !== "albums"
                        Sym {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: search.text !== "" ? "search" : ({ favorites: "heart", videos: "film", screenshots: "monitor", folders: "folder" })[win.route.page] ?? "images"
                            size: 44
                            color: Theme.faint
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            text: {
                                if (parent.reading)
                                    return qsTr("Leyendo la biblioteca…")
                                if (search.text !== "")
                                    return qsTr("Sin resultados para «%1»").arg(search.text)
                                if (parent.wantsFolder)
                                    return qsTr("Tu biblioteca está vacía")
                                switch (win.route.page) {
                                case "favorites": return qsTr("Aún no hay favoritos")
                                case "videos": return qsTr("No hay vídeos")
                                case "screenshots": return qsTr("No hay capturas de pantalla")
                                case "albums": return qsTr("Aún no hay álbumes")
                                case "album": return qsTr("Este álbum está vacío")
                                }
                                return qsTr("No hay nada aquí")
                            }
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Theme.dim
                            font.pixelSize: 13
                            visible: text !== ""
                            text: {
                                if (parent.reading || search.text !== "")
                                    return ""
                                if (parent.wantsFolder)
                                    return App.library.folders.length === 0 ? qsTr("Añade una carpeta con tus fotos y vídeos.") : qsTr("No hay fotos ni vídeos en %1.").arg(App.library.folders.map(folder => qsTr("«%1»").arg(folder.split("/").pop())).join(", "))
                                switch (win.route.page) {
                                case "favorites": return qsTr("Marca una foto con el corazón y aparecerá aquí.")
                                case "albums": return qsTr("Reúne fotos y vídeos sin moverlos de su carpeta.")
                                case "album": return qsTr("Elige fotos en la fototeca y añádelas desde su menú.")
                                }
                                return ""
                            }
                        }
                        PillButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: !parent.reading && search.text === "" && (parent.wantsFolder || win.route.page === "albums")
                            symbol: win.route.page === "albums" ? "plus" : "folder-plus"
                            text: win.route.page === "albums" ? qsTr("Nuevo álbum") : qsTr("Añadir carpeta…")
                            onClicked: {
                                if (win.route.page === "albums")
                                    win.newAlbum([])
                                else
                                    folderDialog.open()
                            }
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
                        parent: Overlay.overlay
                        z: 10
                        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                        anchors.bottom: parent ? parent.bottom : undefined
                        anchors.bottomMargin: 22
                        width: Math.min(win.width - 40, toastText.implicitWidth + 32)
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
            }
        }
    }

    Viewer {
        id: viewer
        anchors.fill: parent
        epoch: win.epoch
        onClosed: {
            if (win.visibility === Window.FullScreen)
                win.showNormal()
            grid.forceActiveFocus()
        }
        onIndexChanged: {
            // The page underneath follows, so leaving lands where the eye is.
            if (visible && !win.cards && grid.photos.paths[index] === path)
                grid.reveal(index)
        }
        onMenuRequested: path => photoMenu.showFor([path])
        onTrashRequested: path => win.trash([path])
        onSaid: message => toast.say(message)
    }

    // --- Pictures for the tests (--shot, --do) -----------------------------------

    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        const paths = grid.photos.paths
        switch (verb) {
        case "page":
            visit(arg)
            break
        case "folder":
            go("folder", folderCards[Number(arg)].key)
            break
        case "group":
            grouping = arg
            break
        case "size":
            sizeStep = Number(arg)
            break
        case "search":
            search.text = arg
            break
        case "select":
            choose(arg.split(",").map(n => paths[Number(n)]), true)
            break
        case "favorite":
            App.albums.setFavorite(arg.split(",").map(n => paths[Number(n)]), true)
            break
        case "album":
            visit("album", App.albums.create(arg, paths.slice(0, 7)))
            break
        case "open":
            openAt(Number(arg))
            break
        case "info":
            viewer.infoOpen = true
            break
        case "zoom":
            viewer.zoomTo(Number(arg))
            break
        case "rotate":
            viewer.rotate(true)
            break
        case "play":
            viewer.togglePlay()
            break
        case "menu":
            photoMenu.showFor([paths[Number(arg)]])
            break
        case "trash":
            trash([paths[Number(arg)]])
            break
        case "name":
            newAlbum([])
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
