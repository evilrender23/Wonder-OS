import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: rabbit

    property url startUrl
    property var startSelect: []
    property bool first: false
    property int cascade: 0

    property url location
    property var backStack: []
    property var forwardStack: []
    /// The view a folder with none of its own is shown in: the last one chosen in this window.
    property string currentView: knownView(App.startView !== "" ? App.startView : Prefs.defaultView)
    property int listSortKey: DirModel.ByName
    property bool listAscending: true
    property int sortOverride: -1
    property string searchText
    property url pendingRename
    property string typeBuffer
    property var menuUrls: []
    property var menuItem: null
    property var openWithApps: []
    // For children whose own property shares the name of one of these ids.
    readonly property alias model: dir
    readonly property alias folderLayout: layout

    readonly property string scheme: String(location).split(":")[0]
    readonly property bool virtualPlace: ["recents", "applications", "tags"].includes(scheme)
    readonly property bool inTrash: scheme === "trash"
    readonly property bool writable: !virtualPlace && !inTrash
    /// Icons can be placed by hand only where the folder is a real one, listed whole.
    readonly property bool canArrange: !virtualPlace && dir.search === ""
    readonly property string view: dir.search !== "" ? "list" : (layout.view !== "" ? knownView(layout.view) : currentView)
    readonly property int iconSize: layout.iconSize > 0 ? layout.iconSize : Prefs.iconSize
    readonly property var pane: paneLoader.item
    readonly property bool typing: activeFocusItem instanceof TextInput
    readonly property int arrangeSortKey: ({ name: DirModel.ByName, kind: DirModel.ByKind, modified: DirModel.ByModified, size: DirModel.BySize })[layout.arrange] ?? DirModel.ByName

    width: App.testing ? App.testSize.width : Prefs.windowWidth
    height: App.testing ? App.testSize.height : Prefs.windowHeight
    minimumWidth: 560
    minimumHeight: 340
    visible: true
    title: App.places.titleFor(location)
    color: App.glass ? "transparent" : Theme.window
    onClosing: {
        layout.flush()
        if (!App.testing) {
            Prefs.windowWidth = width
            Prefs.windowHeight = height
        }
    }

    DirModel {
        id: dir
        url: rabbit.location
        search: rabbit.searchText
        showHidden: Prefs.showHidden
        sortKey: rabbit.sortOverride >= 0 ? rabbit.sortOverride : (rabbit.view === "list" ? rabbit.listSortKey : rabbit.arrangeSortKey)
        // Sorted by date or size, icons lead with the newest and the largest, as lists do.
        ascending: rabbit.view === "list" ? rabbit.listAscending : ![DirModel.ByModified, DirModel.BySize].includes(sortKey)
        onRevealed: row => {
            if (String(rabbit.pendingRename) !== "" && rabbit.same(dir.get(row).url, rabbit.pendingRename)) {
                rabbit.pendingRename = ""
                rabbit.pane.renamingRow = row
            }
        }
    }
    FolderLayout {
        id: layout
        url: rabbit.canArrange ? rabbit.location : ""
    }

    Component.onCompleted: {
        location = String(startUrl) !== "" ? App.places.canonical(startUrl) : App.homeUrl
        if (startSelect.length)
            dir.selectUrls(startSelect)
        if (App.testing && first)
            testStep.start()
    }

    // --- Navigation -------------------------------------------------------

    function knownView(name) {
        return ["icons", "list", "gallery"].includes(name) ? name : "list"
    }
    function same(a, b) {
        const tidy = url => String(url).replace(/([^\/])\/+$/, "$1")
        return tidy(a) === tidy(b)
    }
    function show(url) {
        searchField.text = ""
        searchText = ""
        typeBuffer = ""
        location = url
        if (pane)
            pane.forceActiveFocus()
    }
    function go(url) {
        url = App.places.canonical(url)
        if (String(url) === "" || same(url, location))
            return
        backStack = backStack.concat([location])
        forwardStack = []
        show(url)
    }
    function back() {
        if (!backStack.length)
            return
        const url = backStack[backStack.length - 1]
        forwardStack = forwardStack.concat([location])
        backStack = backStack.slice(0, -1)
        show(url)
    }
    function forward() {
        if (!forwardStack.length)
            return
        const url = forwardStack[forwardStack.length - 1]
        backStack = backStack.concat([location])
        forwardStack = forwardStack.slice(0, -1)
        show(url)
    }
    function up() {
        const from = location
        const parent = App.parentUrl(location)
        if (String(parent) === "" || same(parent, location))
            return
        go(parent)
        dir.selectUrls([from])
    }
    function openRow(row) {
        const item = dir.get(row)
        if (item.url === undefined)
            return
        if (inTrash) {
            toast(qsTr("Items in the Trash can’t be opened. Put them back first."))
        } else if (item.isDir) {
            go(item.url)
        } else {
            App.open(item.url, item.mime, item.isApp)
        }
    }
    function openSelection() {
        const rows = dir.selectedRows()
        if (rows.length === 1) {
            openRow(rows[0])
            return
        }
        for (const row of rows) {
            const item = dir.get(row)
            if (item.isDir)
                App.requestWindow(item.url)
            else
                App.open(item.url, item.mime, item.isApp)
        }
    }

    // --- What the views ask for -------------------------------------------

    function setView(name) {
        currentView = name
        if (canArrange)
            layout.view = name
        if (!App.testing)
            Prefs.defaultView = name
    }
    function setIconSize(size) {
        size = Math.max(32, Math.min(192, Math.round(size / 16) * 16))
        if (canArrange)
            layout.iconSize = size
        if (!App.testing)
            Prefs.iconSize = size
    }
    function namesOf(urls) {
        return urls.map(url => decodeURIComponent(String(url).replace(/\/+$/, "").split("/").pop()))
    }
    function allHere(urls) {
        return urls.every(url => same(App.parentUrl(url), location))
    }
    function beginDrag(item, point, offsets) {
        const urls = dir.selectedUrls()
        if (!urls.length)
            return
        DragState.source = location
        DragState.hotSpot = point
        DragState.offsets = offsets
        DragState.active = true
        dragProxy.uris = urls.map(url => String(url)).join("\r\n")
        item.grabToImage(result => {
            dragProxy.grab = result
            dragProxy.Drag.imageSource = result.url
            dragProxy.Drag.hotSpot = point
            App.announceDrag(true)
            dragProxy.Drag.active = true
        })
    }
    function itemMenu() {
        menuUrls = dir.selectedUrls()
        menuItem = dir.selectedCount === 1 ? dir.get(dir.selectedRows()[0]) : null
        openWithApps = menuItem && !menuItem.isDir && !inTrash ? App.appsFor(menuItem.mime) : []
        itemMenuPopup.popup()
    }
    function backgroundMenu() {
        backgroundMenuPopup.popup()
    }
    function rename() {
        if (dir.selectedCount === 1 && writable && pane)
            pane.renamingRow = dir.selectedRows()[0]
    }
    function commitRename(url, name) {
        const problem = App.ops.rename(url, name)
        if (problem !== "")
            toast(problem)
    }
    function trashSelection() {
        const urls = dir.selectedUrls()
        if (!urls.length || virtualPlace)
            return
        if (inTrash)
            confirm(qsTr("Delete %n item(s) immediately?", "", urls.length), qsTr("This can’t be undone."), qsTr("Delete"), () => App.ops.remove(urls))
        else
            App.ops.trash(urls)
    }
    function confirmEmptyTrash() {
        confirm(qsTr("Empty the Trash?"), qsTr("Everything in it is deleted for good."), qsTr("Empty Trash"), () => App.ops.emptyTrash())
    }
    function confirm(title, detail, action, callback) {
        confirmPopup.title = title
        confirmPopup.detail = detail
        confirmPopup.action = action
        confirmPopup.callback = callback
        confirmPopup.open()
    }
    function newFolder() {
        if (writable)
            App.ops.newFolder(location)
    }
    function quickLook() {
        if (dir.selectedCount)
            App.quickLook(dir.selectedUrls(), rabbit)
    }
    function showInfo(url) {
        infoPopup.info = App.info(url)
        infoPopup.folderSize = ""
        if (infoPopup.info.isDir)
            App.measureFolder(url)
        infoPopup.open()
    }
    function infoForSelection() {
        const urls = dir.selectedUrls()
        showInfo(urls.length ? urls[0] : location)
    }
    function toast(message) {
        toastText.text = message
        toastTimer.restart()
    }
    /// Puts the paths on the clipboard as text, one per line, and says so.
    function copyPath(urls) {
        const paths = urls.map(url => App.displayPath(url)).filter(path => path !== "")
        if (!paths.length)
            return ""
        App.ops.copyText(paths.join("\n"))
        toast(paths.length === 1 ? qsTr("Path copied: %1").arg(paths[0]) : qsTr("%1 paths copied").arg(paths.length))
        return paths.join("\n")
    }
    /// The selection's paths, or this folder's when nothing is selected.
    function copyPathOfSelection() {
        return copyPath(dir.selectedCount ? dir.selectedUrls() : (virtualPlace ? [] : [location]))
    }
    /// Keys every view answers the same way. True if the key was used.
    function handleKey(event) {
        const command = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            openSelection()
            return true
        case Qt.Key_Space:
            if (typeBuffer !== "")
                break
            quickLook()
            return true
        case Qt.Key_Delete:
            trashSelection()
            return true
        case Qt.Key_Backspace:
            if (command & Qt.ControlModifier)
                trashSelection()
            else
                back()
            return true
        case Qt.Key_Escape:
            dir.clearSelection()
            return true
        case Qt.Key_F2:
            rename()
            return true
        case Qt.Key_Up:
            if (command) {
                up()
                return true
            }
            break
        case Qt.Key_Down:
            if (command) {
                openSelection()
                return true
            }
            break
        }
        if (!command && event.text.length === 1 && event.text >= " ") {
            typeBuffer += event.text
            typeTimer.restart()
            const row = dir.typeAhead(typeBuffer)
            if (row >= 0)
                dir.selectOnly(row)
            return true
        }
        return false
    }
    Timer {
        id: typeTimer
        interval: 900
        onTriggered: rabbit.typeBuffer = ""
    }

    Item {
        id: dragProxy
        property string uris
        property var grab: null
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
        Drag.proposedAction: Qt.MoveAction
        Drag.mimeData: ({ "text/uri-list": uris })
        Drag.onDragFinished: {
            Drag.active = false
            DragState.active = false
            App.announceDrag(false)
            grab = null
        }
    }

    Connections {
        target: App.ops
        function onCreated(urls) {
            if (urls.length && rabbit.same(App.parentUrl(urls[0]), rabbit.location))
                dir.selectUrls(urls)
        }
        function onFolderCreated(url) {
            if (!rabbit.active || !rabbit.same(App.parentUrl(url), rabbit.location))
                return
            rabbit.pane.placeAtMenu(rabbit.namesOf([url])[0])
            rabbit.pendingRename = url
            dir.selectUrls([url])
        }
    }
    Connections {
        target: App
        function onFailed(message) {
            if (rabbit.active || rabbit.first)
                rabbit.toast(message)
        }
        function onFolderSized(url, text) {
            if (infoPopup.opened && rabbit.same(url, infoPopup.info.url))
                infoPopup.folderSize = text
        }
    }

    // --- Shortcuts --------------------------------------------------------

    component Key: Shortcut {
        enabled: !rabbit.typing
    }
    Key { sequences: ["Ctrl+N"]; onActivated: App.requestWindow(rabbit.location) }
    Key { sequences: ["Ctrl+Shift+N"]; onActivated: rabbit.newFolder() }
    Shortcut { sequences: ["Ctrl+W"]; onActivated: rabbit.close() }
    Key { sequences: ["Ctrl+C"]; onActivated: if (dir.selectedCount) App.ops.copyToClipboard(dir.selectedUrls()) }
    Key { sequences: ["Ctrl+Shift+C"]; onActivated: rabbit.copyPathOfSelection() }
    Key { sequences: ["Ctrl+X"]; onActivated: if (dir.selectedCount && rabbit.writable) App.ops.cutToClipboard(dir.selectedUrls()) }
    Key { sequences: ["Ctrl+V"]; onActivated: if (rabbit.writable) App.ops.paste(rabbit.location) }
    Key { sequences: ["Ctrl+A"]; onActivated: dir.selectAll() }
    Key { sequences: ["Ctrl+Z"]; onActivated: App.ops.undo() }
    Key { sequences: ["Ctrl+I"]; onActivated: rabbit.infoForSelection() }
    Key { sequences: ["Ctrl+D"]; onActivated: if (dir.selectedCount && rabbit.writable) App.ops.duplicate(dir.selectedUrls()) }
    Shortcut { sequences: ["Ctrl+F"]; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequences: ["Ctrl+L", "Ctrl+Shift+G"]; onActivated: goPopup.open() }
    Shortcut { sequences: ["Ctrl+1"]; onActivated: rabbit.setView("icons") }
    Shortcut { sequences: ["Ctrl+2"]; onActivated: rabbit.setView("list") }
    Shortcut { sequences: ["Ctrl+3"]; onActivated: rabbit.setView("gallery") }
    Shortcut { sequences: ["Ctrl+H", "Ctrl+Shift+."]; onActivated: Prefs.showHidden = !Prefs.showHidden }
    Shortcut { sequences: ["Alt+Left", "Ctrl+["]; onActivated: rabbit.back() }
    Shortcut { sequences: ["Alt+Right", "Ctrl+]"]; onActivated: rabbit.forward() }
    Shortcut { sequences: ["F5", "Ctrl+R"]; onActivated: dir.refresh() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: rabbit.setIconSize(rabbit.iconSize + 16) }
    Shortcut { sequences: ["Ctrl+-"]; onActivated: rabbit.setIconSize(rabbit.iconSize - 16) }
    Shortcut { sequences: ["Ctrl+Shift+Backspace"]; onActivated: if (!App.trashEmpty) rabbit.confirmEmptyTrash() }

    // --- The window -------------------------------------------------------

    Sidebar {
        id: sidebar
        win: rabbit
        visible: Prefs.sidebar
        width: visible ? 196 : 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }
    Rectangle {
        anchors.left: sidebar.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
        z: 3
    }

    Rectangle {
        id: content
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.window

        Item {
            id: toolbar
            width: parent.width
            height: 50

            Row {
                id: leftTools
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                TButton {
                    symbol: "panel-left"
                    onClicked: Prefs.sidebar = !Prefs.sidebar
                }
                TButton {
                    symbol: "chevron-left"
                    active: rabbit.backStack.length > 0
                    onClicked: rabbit.back()
                }
                TButton {
                    symbol: "chevron-right"
                    active: rabbit.forwardStack.length > 0
                    onClicked: rabbit.forward()
                }
            }
            Text {
                anchors.left: leftTools.right
                anchors.leftMargin: 10
                anchors.right: rightTools.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: dir.search !== "" ? qsTr("Searching “%1”").arg(rabbit.title) : rabbit.title
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Row {
                id: rightTools
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Rectangle {
                    width: views.width + 4
                    height: 30
                    radius: 8
                    color: Theme.field
                    anchors.verticalCenter: parent.verticalCenter
                    Row {
                        id: views
                        anchors.centerIn: parent
                        Repeater {
                            model: [
                                { view: "icons", symbol: "layout-grid" },
                                { view: "list", symbol: "list" },
                                { view: "gallery", symbol: "gallery-horizontal-end" },
                            ]
                            TButton {
                                required property var modelData
                                symbol: modelData.symbol
                                checked: rabbit.view === modelData.view
                                active: dir.search === ""
                                onClicked: rabbit.setView(modelData.view)
                            }
                        }
                    }
                }
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "arrow-up-down"
                    active: rabbit.view === "icons" && rabbit.canArrange
                    onClicked: arrangePopup.popup(this, 0, height + 4)
                }
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "circle-ellipsis"
                    onClicked: dir.selectedCount ? rabbit.itemMenu() : rabbit.backgroundMenu()
                }
                TextField {
                    id: searchField
                    anchors.verticalCenter: parent.verticalCenter
                    width: 190
                    height: 28
                    leftPadding: 28
                    rightPadding: 8
                    font.pixelSize: 13
                    color: Theme.text
                    placeholderText: qsTr("Search")
                    placeholderTextColor: Theme.faint
                    selectionColor: Theme.accent
                    selectedTextColor: "white"
                    background: Rectangle {
                        radius: 7
                        color: Theme.field
                        border.color: searchField.activeFocus ? Theme.accent : "transparent"
                        border.width: 2
                        Sym {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            size: 14
                            name: "search"
                            color: Theme.dim
                        }
                    }
                    onTextChanged: searchTimer.restart()
                    Keys.onEscapePressed: {
                        text = ""
                        rabbit.pane.forceActiveFocus()
                    }
                    Keys.onDownPressed: rabbit.pane.forceActiveFocus()
                    Timer {
                        id: searchTimer
                        interval: 250
                        onTriggered: rabbit.searchText = searchField.text.trim()
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        Loader {
            id: paneLoader
            anchors.top: toolbar.bottom
            anchors.bottom: pathBar.top
            anchors.left: parent.left
            anchors.right: parent.right
            focus: true
            sourceComponent: rabbit.view === "icons" ? iconPane : (rabbit.view === "gallery" ? galleryPane : listPane)
            onLoaded: item.forceActiveFocus()
        }
        Component {
            id: iconPane
            IconView {
                win: rabbit
                dir: rabbit.model
                layout: rabbit.folderLayout
                iconSize: rabbit.iconSize
            }
        }
        Component {
            id: listPane
            ListPane {
                win: rabbit
                dir: rabbit.model
            }
        }
        Component {
            id: galleryPane
            GalleryPane {
                win: rabbit
                dir: rabbit.model
            }
        }

        Column {
            anchors.centerIn: paneLoader
            visible: dir.count === 0 && !dir.loading
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: dir.error !== "" ? dir.error : (dir.search !== "" ? qsTr("No Results") : (rabbit.inTrash ? qsTr("The Trash is empty") : qsTr("This folder is empty")))
                color: Theme.faint
                font.pixelSize: 18
            }
        }

        Rectangle {
            id: pathBar
            visible: Prefs.pathBar && !rabbit.virtualPlace
            anchors.bottom: statusBar.top
            width: parent.width
            height: visible ? 24 : 0
            color: Theme.chrome
            clip: true
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Row {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Repeater {
                    id: crumbs
                    model: App.crumbs(rabbit.location)
                    Row {
                        id: crumb
                        required property var modelData
                        required property int index
                        spacing: 4
                        Sym {
                            visible: crumb.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            size: 11
                            name: "chevron-right"
                            color: Theme.faint
                        }
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 14
                            sourceSize: Qt.size(28, 28)
                            source: App.icons + crumb.modelData.icon
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: crumb.modelData.title
                            color: Theme.dim
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                // Right click: this crumb's path, without going there.
                                onClicked: mouse => mouse.button === Qt.RightButton ? rabbit.copyPath([crumb.modelData.url]) : rabbit.go(crumb.modelData.url)
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: copyPathButton
                anchors.right: parent.right
                y: 1
                width: 30
                height: parent.height - 1
                // Opaque: a long path slides under the button instead of over it.
                color: Theme.chrome
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 5
                    color: Theme.hover
                    visible: copyPathArea.containsMouse
                }
                Sym {
                    anchors.centerIn: parent
                    size: 12
                    name: "copy"
                    color: copyPathArea.containsMouse ? Theme.text : Theme.dim
                }
                MouseArea {
                    id: copyPathArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: rabbit.copyPath([rabbit.location])
                }
            }
        }

        Rectangle {
            id: statusBar
            visible: Prefs.statusBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: visible ? 24 : 0
            color: Theme.chrome
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Text {
                anchors.centerIn: parent
                color: Theme.dim
                font.pixelSize: 11
                text: {
                    if (App.ops.busy)
                        return App.ops.status
                    const items = dir.selectedCount > 0 ? qsTr("%1 of %2 selected").arg(dir.selectedCount).arg(dir.count) : qsTr("%n item(s)", "", dir.count)
                    const free = rabbit.scheme === "file" ? App.freeSpace(rabbit.location) : ""
                    return free !== "" ? items + ", " + free : items
                }
            }
            Rectangle {
                visible: App.ops.busy
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * App.ops.progress
                color: Theme.accent
            }
            Slider {
                id: sizeSlider
                visible: rabbit.view === "icons"
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 110
                height: 16
                from: 32
                to: 192
                stepSize: 16
                snapMode: Slider.SnapAlways
                value: rabbit.iconSize
                onMoved: rabbit.setIconSize(value)
                background: Rectangle {
                    y: sizeSlider.height / 2 - 1
                    width: sizeSlider.width
                    height: 3
                    radius: 2
                    color: Theme.border
                }
                handle: Rectangle {
                    x: sizeSlider.visualPosition * (sizeSlider.width - width)
                    y: (sizeSlider.height - height) / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.dark ? "#d0d0d0" : "white"
                    border.color: Theme.faint
                }
            }
        }

        Rectangle {
            id: toastBox
            visible: toastTimer.running
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 62
            width: Math.min(parent.width - 60, toastText.implicitWidth + 28)
            height: toastText.height + 16
            radius: 9
            color: Theme.dark ? "#f0f0f0" : "#2c2c2e"
            z: 10
            Text {
                id: toastText
                anchors.centerIn: parent
                width: parent.width - 28
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.dark ? "#1d1d1f" : "white"
                font.pixelSize: 12
            }
            Timer {
                id: toastTimer
                interval: 4500
            }
        }
    }

    // --- Menus ------------------------------------------------------------

    ArrangeMenu {
        id: arrangePopup
        win: rabbit
        layout: rabbit.folderLayout
    }

    FMenu {
        id: itemMenuPopup
        FMenuItem {
            text: qsTr("Open")
            visible: !rabbit.inTrash
            onTriggered: rabbit.openSelection()
        }
        FMenu {
            id: openWithMenu
            title: qsTr("Open With")
            enabled: rabbit.openWithApps.length > 0
            Instantiator {
                model: rabbit.openWithApps
                delegate: FMenuItem {
                    required property var modelData
                    text: modelData.name
                    checked: modelData.isDefault
                    onTriggered: App.openWith(rabbit.menuUrls, modelData.id, rabbit.menuItem.mime)
                }
                onObjectAdded: (index, object) => openWithMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => openWithMenu.removeItem(object)
            }
        }
        FMenuItem {
            text: qsTr("Open in New Window")
            visible: rabbit.menuItem !== null && rabbit.menuItem.isDir && !rabbit.inTrash
            onTriggered: App.requestWindow(rabbit.menuItem.url)
        }
        FMenuItem {
            text: qsTr("Put Back")
            visible: rabbit.inTrash
            onTriggered: App.ops.restore(rabbit.menuUrls)
        }
        FSeparator {}
        FMenuItem {
            text: rabbit.inTrash ? qsTr("Delete Immediately…") : qsTr("Move to Trash")
            enabled: !rabbit.virtualPlace
            onTriggered: rabbit.trashSelection()
        }
        FMenuItem {
            text: qsTr("Empty Trash")
            visible: rabbit.inTrash
            enabled: !App.trashEmpty
            onTriggered: rabbit.confirmEmptyTrash()
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Get Info")
            onTriggered: rabbit.infoForSelection()
        }
        FMenuItem {
            text: qsTr("Rename")
            visible: rabbit.menuItem !== null && rabbit.writable
            onTriggered: rabbit.rename()
        }
        FMenuItem {
            text: qsTr("Compress")
            visible: rabbit.writable
            onTriggered: App.ops.compress(rabbit.menuUrls)
        }
        FMenuItem {
            text: qsTr("Extract Here")
            visible: rabbit.menuItem !== null && rabbit.writable && App.ops.isArchive(rabbit.menuItem.mime)
            onTriggered: App.ops.extract(rabbit.menuItem.url)
        }
        FMenuItem {
            text: qsTr("Duplicate")
            visible: rabbit.writable
            onTriggered: App.ops.duplicate(rabbit.menuUrls)
        }
        FMenuItem {
            text: qsTr("Make Alias")
            visible: rabbit.writable
            onTriggered: App.ops.makeAlias(rabbit.menuUrls)
        }
        FMenuItem {
            text: qsTr("Quick Look")
            visible: rabbit.menuItem !== null
            onTriggered: rabbit.quickLook()
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Copy")
            onTriggered: App.ops.copyToClipboard(rabbit.menuUrls)
        }
        FMenuItem {
            text: qsTr("Cut")
            visible: rabbit.writable
            onTriggered: App.ops.cutToClipboard(rabbit.menuUrls)
        }
        FMenuItem {
            text: qsTr("Copy Path")
            visible: rabbit.menuItem !== null && rabbit.menuItem.path !== ""
            onTriggered: rabbit.copyPath(rabbit.menuUrls)
        }
        FSeparator {
            visible: tagRow.visible
        }
        Item {
            id: tagRow
            visible: !rabbit.inTrash && rabbit.scheme !== "applications"
            implicitHeight: visible ? 30 : 0
            implicitWidth: 200
            Row {
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9
                Repeater {
                    model: App.tagNames
                    Rectangle {
                        id: dot
                        required property string modelData
                        // Read when the menu opens: the answer depends on what is selected.
                        readonly property bool on: itemMenuPopup.opened && App.allTagged(rabbit.menuUrls, modelData)
                        width: 16
                        height: 16
                        radius: 8
                        color: App.tagColor(modelData)
                        border.color: Theme.text
                        border.width: on ? 2 : 0
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -3
                            onClicked: {
                                App.setTag(rabbit.menuUrls, dot.modelData, !dot.on)
                                itemMenuPopup.close()
                            }
                        }
                    }
                }
            }
        }
        FMenuItem {
            text: qsTr("Customize Folder…")
            visible: rabbit.menuItem !== null && rabbit.menuItem.isDir && rabbit.menuItem.path !== "" && !rabbit.inTrash
            onTriggered: App.customizeFolder(rabbit.menuItem.url)
        }
        FSeparator {
            visible: favourite.visible
        }
        FMenuItem {
            id: favourite
            visible: rabbit.menuItem !== null && rabbit.menuItem.isDir && rabbit.writable && itemMenuPopup.opened && !App.places.isFavorite(rabbit.menuItem.url)
            text: qsTr("Add to Sidebar")
            onTriggered: App.places.addFavorite(rabbit.menuItem.url)
        }
    }

    FMenu {
        id: backgroundMenuPopup
        FMenuItem {
            text: qsTr("New Folder")
            enabled: rabbit.writable
            onTriggered: rabbit.newFolder()
        }
        FMenuItem {
            text: qsTr("Empty Trash")
            visible: rabbit.inTrash
            enabled: !App.trashEmpty
            onTriggered: rabbit.confirmEmptyTrash()
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Get Info")
            enabled: !rabbit.virtualPlace
            onTriggered: rabbit.showInfo(rabbit.location)
        }
        FMenuItem {
            text: qsTr("Paste")
            enabled: rabbit.writable && App.ops.canPaste
            onTriggered: App.ops.paste(rabbit.location)
        }
        FMenuItem {
            text: qsTr("Copy Folder Path")
            enabled: !rabbit.virtualPlace
            onTriggered: rabbit.copyPath([rabbit.location])
        }
        FMenuItem {
            text: App.ops.undoText !== "" ? App.ops.undoText : qsTr("Undo")
            enabled: App.ops.canUndo
            onTriggered: App.ops.undo()
        }
        FSeparator {}
        ArrangeMenu {
            win: rabbit
            layout: rabbit.folderLayout
        }
        FMenuItem {
            text: qsTr("Clean Up")
            enabled: rabbit.view === "icons" && rabbit.canArrange && ["", "grid"].includes(layout.arrange)
            onTriggered: rabbit.pane.cleanUp()
        }
        FMenu {
            title: qsTr("Clean Up By")
            enabled: rabbit.view === "icons" && rabbit.canArrange && ["", "grid"].includes(layout.arrange)
            FMenuItem {
                text: qsTr("Name")
                onTriggered: rabbit.pane.cleanUpBy(DirModel.ByName)
            }
            FMenuItem {
                text: qsTr("Kind")
                onTriggered: rabbit.pane.cleanUpBy(DirModel.ByKind)
            }
            FMenuItem {
                text: qsTr("Date Modified")
                onTriggered: rabbit.pane.cleanUpBy(DirModel.ByModified)
            }
            FMenuItem {
                text: qsTr("Size")
                onTriggered: rabbit.pane.cleanUpBy(DirModel.BySize)
            }
        }
        FMenuItem {
            text: qsTr("Reset Icon Positions")
            enabled: rabbit.view === "icons" && layout.arranged
            onTriggered: layout.reset()
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Show Hidden Files")
            checkable: true
            checked: Prefs.showHidden
            onTriggered: Prefs.showHidden = !Prefs.showHidden
        }
        FMenuItem {
            text: qsTr("Show Path Bar")
            checkable: true
            checked: Prefs.pathBar
            onTriggered: Prefs.pathBar = !Prefs.pathBar
        }
        FMenuItem {
            text: qsTr("Open in Terminal")
            enabled: rabbit.scheme === "file"
            onTriggered: App.openTerminal(rabbit.location)
        }
    }

    // --- Panels -----------------------------------------------------------

    component Panel: Popup {
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 18
        Overlay.modal: Rectangle {
            color: "#40000000"
        }
        background: Rectangle {
            radius: 12
            color: Theme.menu
            border.color: Theme.border
        }
        onClosed: if (rabbit.pane) rabbit.pane.forceActiveFocus()
    }
    component PanelButton: Rectangle {
        id: panelButton
        property string text
        property bool primary: false
        signal clicked()
        width: Math.max(86, buttonText.implicitWidth + 28)
        height: 28
        radius: 7
        color: primary ? Theme.accent : Theme.field
        Text {
            id: buttonText
            anchors.centerIn: parent
            text: panelButton.text
            color: panelButton.primary ? "white" : Theme.text
            font.pixelSize: 13
        }
        MouseArea {
            anchors.fill: parent
            onClicked: panelButton.clicked()
        }
    }

    Panel {
        id: confirmPopup
        property string title
        property string detail
        property string action
        property var callback: null
        width: 340
        contentItem: Column {
            spacing: 8
            focus: true
            Keys.onReturnPressed: {
                confirmPopup.close()
                confirmPopup.callback()
            }
            Text {
                width: parent.width
                text: confirmPopup.title
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                width: parent.width
                text: confirmPopup.detail
                wrapMode: Text.Wrap
                color: Theme.dim
                font.pixelSize: 12
            }
            Row {
                anchors.right: parent.right
                topPadding: 8
                spacing: 8
                PanelButton {
                    text: qsTr("Cancel")
                    onClicked: confirmPopup.close()
                }
                PanelButton {
                    text: confirmPopup.action
                    primary: true
                    onClicked: {
                        confirmPopup.close()
                        confirmPopup.callback()
                    }
                }
            }
        }
    }

    Panel {
        id: goPopup
        width: 420
        onOpened: {
            goField.text = App.displayPath(rabbit.location)
            goField.selectAll()
            goField.forceActiveFocus()
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: qsTr("Go to the folder:")
                color: Theme.text
                font.pixelSize: 13
            }
            TextField {
                id: goField
                width: parent.width
                font.pixelSize: 13
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: "white"
                background: Rectangle {
                    radius: 6
                    color: Theme.field
                    border.color: Theme.accent
                    border.width: 2
                }
                onAccepted: {
                    const url = App.folderFromInput(text, rabbit.location)
                    goPopup.close()
                    if (String(url) === "")
                        rabbit.toast(qsTr("The folder can’t be found."))
                    else
                        rabbit.go(url)
                }
            }
        }
    }

    Panel {
        id: infoPopup
        property var info: ({})
        property string folderSize
        width: 340
        contentItem: Column {
            spacing: 10
            Row {
                spacing: 12
                Image {
                    width: 56
                    height: 56
                    sourceSize: Qt.size(112, 112)
                    source: infoPopup.info.icon ? App.icons + infoPopup.info.icon : ""
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: 230
                        text: infoPopup.info.name ?? ""
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Text {
                        text: infoPopup.info.isDir ? (infoPopup.folderSize !== "" ? infoPopup.folderSize : qsTr("Calculating size…")) : (infoPopup.info.sizeShort ?? "")
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Repeater {
                model: [
                    [qsTr("Kind"), infoPopup.info.kind],
                    [qsTr("Size"), infoPopup.info.isDir ? infoPopup.folderSize : infoPopup.info.size],
                    [qsTr("Where"), infoPopup.info.where],
                    [qsTr("Created"), infoPopup.info.created],
                    [qsTr("Modified"), infoPopup.info.modified],
                    [qsTr("Last opened"), infoPopup.info.accessed],
                    [qsTr("Dimensions"), infoPopup.info.dimensions],
                    [qsTr("Original"), infoPopup.info.original],
                    [qsTr("Owner"), infoPopup.info.owner ? infoPopup.info.owner + " · " + infoPopup.info.ownerAccess : undefined],
                    [qsTr("Group"), infoPopup.info.group ? infoPopup.info.group + " · " + infoPopup.info.groupAccess : undefined],
                    [qsTr("Everyone"), infoPopup.info.otherAccess],
                ].filter(line => line[1] !== undefined && line[1] !== "")
                Row {
                    required property var modelData
                    spacing: 8
                    Text {
                        width: 86
                        horizontalAlignment: Text.AlignRight
                        text: modelData[0] + ":"
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                    Text {
                        width: 210
                        text: modelData[1]
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // --- Test runs: perform the actions given on the command line, then save a picture.

    property var testQueue: App.testActions.slice()
    Timer {
        id: testStep
        interval: 250
        repeat: true
        onTriggered: {
            if (dir.loading)
                return
            if (rabbit.testQueue.length) {
                rabbit.perform(rabbit.testQueue.shift())
                return
            }
            stop()
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 900
        onTriggered: {
            layout.flush()
            App.saveShot(rabbit)
        }
    }
    function perform(action) {
        const parts = action.trim().split(":")
        const rest = parts.slice(1).join(":")
        switch (parts[0]) {
        case "select":
            dir.selectRows(parts.slice(1).map(name => dir.rowForFileName(name)).filter(row => row >= 0))
            break
        case "move": {
            const row = dir.rowForFileName(parts[1])
            if (!dir.isSelected(row))
                dir.selectOnly(row)
            const names = dir.selectedRows().map(other => dir.get(other).fileName)
            const origin = pane.positions[parts[1]]
            const offsets = {}
            for (const name of names)
                offsets[name] = Qt.point(pane.positions[name].x - origin.x, pane.positions[name].y - origin.y)
            pane.moveGroup(names, offsets, Qt.point(Number(parts[2]), Number(parts[3])))
            break
        }
        case "drop": {
            // What a drag of the selection ends in (the offscreen platform has no drags of its own):
            // the grabbed icon is let go with its cell's corner at the given point.
            const row = dir.rowForFileName(parts[1])
            if (!dir.isSelected(row))
                dir.selectOnly(row)
            const origin = pane.positions[parts[1]]
            const offsets = {}
            for (const other of dir.selectedRows()) {
                const name = dir.get(other).fileName
                offsets[name] = Qt.point(pane.positions[name].x - origin.x, pane.positions[name].y - origin.y)
            }
            DragState.source = location
            DragState.hotSpot = Qt.point(40, 30)
            DragState.offsets = offsets
            DragState.active = true
            pane.handleDrop({
                x: pane.pad + Number(parts[2]) + 40,
                y: pane.pad + Number(parts[3]) + 30,
                urls: dir.selectedUrls(),
                proposedAction: Qt.MoveAction,
                accept: () => {},
                acceptProposedAction: () => {}
            })
            DragState.active = false
            break
        }
        case "view":
            setView(parts[1])
            break
        case "menu":
            // The selection's context menu, with "Open With" unfolded.
            itemMenu()
            openWithMenu.popup(itemMenuPopup.x + itemMenuPopup.width, itemMenuPopup.y + 26)
            break
        case "open":
            // What a double click on the item does.
            openRow(dir.rowForFileName(parts[1]))
            break
        case "openwith":
            menuUrls = dir.selectedUrls()
            menuItem = dir.get(dir.selectedRows()[0])
            App.openWith(menuUrls, rest, menuItem.mime)
            break
        case "arrange":
            layout.arrange = parts[1] ?? ""
            break
        case "cleanup":
            if (parts[1])
                pane.cleanUpBy({ name: DirModel.ByName, kind: DirModel.ByKind, modified: DirModel.ByModified, size: DirModel.BySize }[parts[1]])
            else
                pane.cleanUp()
            break
        case "reset":
            layout.reset()
            break
        case "size":
            setIconSize(Number(parts[1]))
            break
        case "go":
            go(App.folderFromInput(rest, location))
            break
        case "back":
            back()
            break
        case "search":
            searchField.text = rest
            break
        case "hidden":
            dir.showHidden = true
            break
        case "info":
            infoForSelection()
            break
        case "look":
            quickLook()
            break
        case "menu":
            dir.selectedCount ? itemMenu() : backgroundMenu()
            break
        case "rename":
            rename()
            break
        case "customize":
            App.customizeFolder(dir.get(dir.selectedRows()[0]).url)
            break
        case "bgmenu":
            backgroundMenu()
            break
        case "copypath":
            // Ctrl+Shift+C; "copypath:crumb:N" is a right click on the Nth crumb.
            console.log("COPIED " + (parts[1] === "crumb" ? copyPath([crumbs.model[Number(parts[2])].url]) : copyPathOfSelection()))
            break
        case "newfolder":
            newFolder()
            break
        case "dump":
            console.log("LAYOUT " + dir.fileNames().map(name => name + "=" + pane.positions[name].x + "," + pane.positions[name].y).join(" "))
            break
        default:
            console.warn("unknown test action", action)
        }
    }
}
