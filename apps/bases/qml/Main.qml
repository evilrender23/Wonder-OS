import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

ApplicationWindow {
    id: win

    property bool sidebarOpen: App.setting("sidebar", true)
    property bool inspectorOpen: App.setting("inspector", false)
    /// Nothing but the content: no bars, no panels.
    property bool focusMode: false
    property real zoom: 1.0
    /// What the sidebar's search field holds.
    property string sideFilter
    readonly property bool hasDb: App.db.isOpen
    readonly property int current: tabs.currentIndex
    readonly property var page: tabs.count > 0 && pages.itemAt(current) ? pages.itemAt(current).item : null
    readonly property var tab: tabs.count > 0 && current >= 0 && current < tabs.count ? tabs.get(current) : null
    readonly property bool dirty: {
        for (let i = 0; i < pages.count; ++i) {
            const loader = pages.itemAt(i)
            if (loader && loader.item && loader.item.dirty)
                return true
        }
        return false
    }
    readonly property bool busy: palette.visible || confirmDialog.visible || nameDialog.visible || pickDialog.visible
        || importDialog.visible || versionsPanel.visible
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit

    width: App.testing ? App.testSize.width : App.setting("width", 1180)
    height: App.testing ? App.testSize.height : App.setting("height", 740)
    minimumWidth: 860
    minimumHeight: 520
    visible: true
    title: hasDb ? App.db.name + " — Bases" : "Bases"
    color: App.glass ? "transparent" : Theme.window

    onClosing: {
        if (App.testing)
            return
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("sidebar", sidebarOpen)
        App.setSetting("inspector", inspectorOpen)
    }
    Component.onCompleted: {
        if (App.testing)
            testStep.start()
    }

    Connections {
        target: App
        function onSaid(message) {
            win.say(message)
        }
    }
    Connections {
        target: App.db
        // Another file: the tabs of the one before mean nothing here.
        function onOpened() {
            tabs.clear()
            tabs.currentIndex = -1
            win.sideFilter = ""
            win.fresh = App.db.isOpen
            win.openFirst()
        }
        // A template fills the file after it is opened.
        function onSchemaChanged() {
            win.openFirst()
        }
    }
    /// Just opened: its first table has not been shown yet.
    property bool fresh: false
    function openFirst() {
        if (!fresh || !App.db.isOpen)
            return
        const names = App.db.tableNames(true)
        if (names.length > 0) {
            fresh = false
            openObject("table", names[0])
        }
    }

    // --- What the host of a page offers --------------------------------------------

    function say(message) {
        toast.say(message)
    }
    function confirm(title, text, action, callback) {
        confirmDialog.ask(title, text, action, callback)
    }
    function askName(title, text, name, action, callback) {
        nameDialog.ask(title, text, name, action, callback)
    }
    function askPick(title, options, callback) {
        pickDialog.ask(title, options, callback)
    }
    /// Asks where to write `name` to, and gives the path to `callback`.
    function askExport(name, callback, filters) {
        exportDialog.then = callback
        exportDialog.nameFilters = filters ?? ["CSV (*.csv)", "JSON (*.json)", "SQL (*.sql)", "Texto con tabuladores (*.tsv)"]
        exportDialog.currentFolder = App.fileUrl(App.folder)
        exportDialog.selectedFile = App.fileUrl(App.folder + "/" + name + "." + exportDialog.nameFilters[0].match(/\*\.(\w+)/)[1])
        exportDialog.open()
    }
    function tableSaved(key, was, name) {
        for (let i = 0; i < tabs.count; ++i) {
            if (tabs.get(i).key === key) {
                tabs.setProperty(i, "name", name)
                tabs.setProperty(i, "key", "table:" + name)
            }
        }
        say(was === "" ? "Tabla «" + name + "» creada." : "Tabla «" + name + "» guardada.")
    }

    /// A query, a form or a report was given a name, or saved again.
    function objectSaved(key, kind, id, name) {
        for (let i = 0; i < tabs.count; ++i) {
            if (tabs.get(i).key === key) {
                tabs.setProperty(i, "name", name)
                tabs.setProperty(i, "objectId", id)
                tabs.setProperty(i, "key", kind + "#" + id)
            }
        }
        say("«" + name + "» guardado.")
    }

    // --- Tabs ----------------------------------------------------------------------

    ListModel {
        id: tabs
        property int currentIndex: -1
    }
    property int untitled: 0

    /// Opens (or goes to) a table, a query, a form, a report or the relations.
    /// `id` is the object's own, for the ones Bases keeps; `extra` goes to the page.
    function openObject(kind, name, id, extra) {
        const key = kind === "table" ? "table:" + name : (id > 0 ? kind + "#" + id : (kind === "relations" ? "relations" : kind + "+" + (++untitled)))
        for (let i = 0; i < tabs.count; ++i) {
            if (tabs.get(i).key === key) {
                tabs.currentIndex = i
                if (extra && win.page && win.page.apply)
                    win.page.apply(extra)
                return
            }
        }
        tabs.append({key: key, kind: kind, name: name ?? "", objectId: id ?? 0, extra: extra ? JSON.stringify(extra) : ""})
        tabs.currentIndex = tabs.count - 1
    }
    function closeTab(index) {
        if (index < 0 || index >= tabs.count)
            return
        const loader = pages.itemAt(index)
        const leave = () => {
            tabs.remove(index)
            tabs.currentIndex = Math.min(tabs.count - 1, index <= tabs.currentIndex ? Math.max(0, tabs.currentIndex - (index < tabs.currentIndex ? 1 : 0)) : tabs.currentIndex)
        }
        if (loader && loader.item && loader.item.dirty)
            confirm("¿Cerrar «" + tabTitle(tabs.get(index)) + "» sin guardar?", "Los cambios que no se han guardado se perderán.", "Cerrar", leave)
        else
            leave()
    }
    function closeObject(key) {
        for (let i = tabs.count - 1; i >= 0; --i) {
            if (tabs.get(i).key === key) {
                tabs.remove(i)
                tabs.currentIndex = Math.min(tabs.count - 1, Math.max(0, tabs.currentIndex - (i < tabs.currentIndex ? 1 : 0)))
            }
        }
    }
    function tabTitle(entry) {
        if (!entry)
            return ""
        if (entry.kind === "relations")
            return "Relaciones"
        if (entry.name !== "")
            return entry.name
        return ({table: "Tabla nueva", query: "Consulta nueva", form: "Formulario nuevo", report: "Informe nuevo"})[entry.kind] ?? ""
    }
    function stepTab(by) {
        if (tabs.count > 0)
            tabs.currentIndex = (tabs.currentIndex + by + tabs.count) % tabs.count
    }
    /// A number with its thousands apart.
    function count(n) {
        return Number(n).toLocaleString(Qt.locale("es_ES"), "f", 0)
    }
    /// Forwards a verb to the page in sight.
    function pageAct(verb, arg) {
        return page && page.act ? page.act(verb, arg ?? "") : false
    }

    // --- Files ---------------------------------------------------------------------

    function newDatabase(templateId) {
        const all = App.templates
        const chosen = all.find(t => t.id === templateId)
        const path = App.freePath(chosen ? chosen.name : "Sin título")
        if (hasDb && !App.testing) {
            App.openWindow(["--new", templateId || "blank"])
            return
        }
        App.createPath(path, templateId || "")
    }
    function openFile(path) {
        if (hasDb && !App.testing && path !== App.db.path)
            App.openWindow([path])
        else
            App.openPath(path)
    }
    function renameDatabase(name) {
        const error = App.renameDatabase(name)
        if (error !== "")
            say(error)
    }
    function saveAll() {
        if (!hasDb)
            return
        for (let i = 0; i < pages.count; ++i) {
            const loader = pages.itemAt(i)
            if (loader && loader.item && loader.item.dirty && loader.item.save)
                loader.item.save()
        }
        const error = App.saveVersion()
        say(error !== "" ? error : "Guardado. Se ha conservado una versión.")
    }
    function dropObject(kind, name, id) {
        if (kind === "table") {
            const view = App.db.isView(name)
            confirm("¿Eliminar " + (view ? "la vista" : "la tabla") + " «" + name + "»?",
                view ? "La consulta que la define se perderá." : "Se eliminarán sus " + App.db.rowCount(name) + " registros. Esto no se puede deshacer.",
                "Eliminar", () => {
                    const error = App.dropTable(name)
                    if (error !== "")
                        say(error)
                    else
                        closeObject("table:" + name)
                })
            return
        }
        confirm("¿Eliminar «" + name + "»?", "Esto no se puede deshacer.", "Eliminar", () => {
            App.db.removeObject(id)
            closeObject(kind + "#" + id)
        })
    }
    function renameObject(kind, name, id) {
        askName(kind === "table" ? "Renombrar la tabla" : "Renombrar", "", name, "Renombrar", next => {
            if (next === name)
                return ""
            if (kind === "table") {
                const error = App.renameTable(name, next)
                if (error !== "")
                    return error
                for (let i = 0; i < tabs.count; ++i) {
                    if (tabs.get(i).key === "table:" + name) {
                        tabs.setProperty(i, "name", next)
                        tabs.setProperty(i, "key", "table:" + next)
                    }
                }
                return ""
            }
            if (!App.db.renameObject(id, next))
                return App.db.lastError
            for (let i = 0; i < tabs.count; ++i) {
                if (tabs.get(i).key === kind + "#" + id)
                    tabs.setProperty(i, "name", next)
            }
            return ""
        })
    }

    // --- The commands: the palette, the keys and --do all read this list -------------

    readonly property var actions: {
        const open = hasDb
        const kind = tab ? tab.kind : ""
        const table = kind === "table"
        const p = page
        const data = table && !!p && p.mode !== "design"
        return [
            {id: "palette", title: "Órdenes…", section: "Ir a", shortcut: "Ctrl+K", symbol: "command", hidden: true, run: arg => palette.show(arg)},
            {id: "new", title: "Base de datos nueva", section: "Archivo", shortcut: "Ctrl+N", symbol: "file-plus", run: arg => newDatabase(arg)},
            {id: "open", title: "Abrir…", section: "Archivo", shortcut: "Ctrl+O", symbol: "folder-open", run: arg => arg ? openFile(arg) : openDialog.open()},
            {id: "sample", title: "Abrir la base de ejemplo", section: "Archivo", symbol: "database", run: () => App.openSample()},
            {id: "save", title: "Guardar y conservar una versión", section: "Archivo", shortcut: "Ctrl+S", symbol: "save", enabled: open, run: () => saveAll()},
            {id: "versions", title: "Versiones…", section: "Archivo", symbol: "history", enabled: open, run: () => versionsPanel.show()},
            {id: "rename", title: "Renombrar la base de datos…", section: "Archivo", symbol: "pencil", enabled: open, run: arg => arg ? renameDatabase(arg) : titleBox.rename()},
            {id: "import", title: "Importar CSV…", section: "Archivo", symbol: "download", enabled: open && !App.db.readOnly, run: arg => arg ? importDialog.show(arg) : importFile.open()},
            {id: "export", title: "Exportar lo que se ve…", section: "Archivo", shortcut: "Ctrl+E", symbol: "upload", enabled: open && !!p && !!p.exportTo, run: arg => arg ? p.exportTo(arg) : askExport(tabTitle(tab), path => p.exportTo(path), p.exportFilters)},
            {id: "dump", title: "Exportar toda la base como SQL…", section: "Archivo", symbol: "upload", enabled: open, run: arg => {
                const write = path => {
                    const error = App.exportDump(path)
                    say(error !== "" ? error : "Base exportada a " + path)
                }
                if (arg)
                    write(arg)
                else
                    askExport(App.db.name, write, ["SQL (*.sql)"])
            }},
            {id: "close", title: "Cerrar la base de datos", section: "Archivo", symbol: "x", enabled: open, run: () => App.closeDatabase()},

            {id: "newtable", title: "Tabla nueva", section: "Crear", shortcut: "Ctrl+T", symbol: "table", enabled: open && !App.db.readOnly, run: () => openObject("table", "")},
            {id: "newquery", title: "Consulta nueva", section: "Crear", shortcut: "Ctrl+Shift+Q", symbol: "code", enabled: open, run: arg => openObject("query", "", 0, arg ? {sql: arg} : null)},
            {id: "newform", title: "Formulario nuevo…", section: "Crear", symbol: "form", enabled: open, run: arg => newFromTable("form", arg)},
            {id: "newreport", title: "Informe nuevo…", section: "Crear", symbol: "file-text", enabled: open, run: arg => newFromTable("report", arg)},

            {id: "addrow", title: "Añadir registro", section: "Registros", shortcut: "Ctrl+Return", symbol: "plus", enabled: data && p.editable, run: () => pageAct("addrow")},
            {id: "delrow", title: "Eliminar los registros seleccionados", section: "Registros", shortcut: "Ctrl+Backspace", symbol: "trash-2", enabled: data && p.editable, run: () => pageAct("delrow")},
            {id: "duprow", title: "Duplicar el registro", section: "Registros", shortcut: "Ctrl+D", symbol: "copy", enabled: data && p.editable, run: () => pageAct("duprow")},
            {id: "undo", title: App.db.canUndo ? "Deshacer: " + App.db.undoText : "Deshacer", section: "Registros", shortcut: "Ctrl+Z", symbol: "undo-2", enabled: App.db.canUndo, run: () => App.db.undo()},
            {id: "redo", title: App.db.canRedo ? "Rehacer: " + App.db.redoText : "Rehacer", section: "Registros", shortcut: "Ctrl+Shift+Z", symbol: "redo-2", enabled: App.db.canRedo, run: () => App.db.redo()},
            {id: "find", title: "Buscar en la tabla", section: "Registros", shortcut: "Ctrl+F", symbol: "search", enabled: data, run: arg => pageAct("search", arg)},
            {id: "addfilter", title: "Añadir un filtro", section: "Registros", shortcut: "Ctrl+Shift+L", symbol: "list-filter", enabled: data, run: arg => pageAct("pill", arg)},
            {id: "clearfilters", title: "Quitar los filtros", section: "Registros", symbol: "filter-x", enabled: data, run: () => pageAct("clear")},
            {id: "refresh", title: "Actualizar", section: "Registros", shortcut: "F5", symbol: "refresh-cw", enabled: open, run: () => pageAct("refresh")},

            {id: "grid", title: "Ver como rejilla", section: "Vista", shortcut: "Ctrl+1", symbol: "rows-3", enabled: table, run: () => pageAct("view", "grid")},
            {id: "cards", title: "Ver como tarjetas", section: "Vista", shortcut: "Ctrl+2", symbol: "layout-grid", enabled: table, run: () => pageAct("view", "cards")},
            {id: "board", title: "Ver como tablero", section: "Vista", shortcut: "Ctrl+3", symbol: "kanban", enabled: table, run: arg => pageAct("view", "board" + (arg ? ":" + arg : ""))},
            {id: "form", title: "Ver como formulario", section: "Vista", shortcut: "Ctrl+4", symbol: "form", enabled: table, run: () => pageAct("view", "form")},
            {id: "design", title: "Diseño de la tabla", section: "Vista", shortcut: "Ctrl+5", symbol: "columns-3", enabled: table, run: () => pageAct("view", "design")},
            {id: "relations", title: "Relaciones", section: "Vista", shortcut: "Ctrl+R", symbol: "workflow", enabled: open, run: () => openObject("relations", "")},
            {id: "sidebar", title: "Mostrar u ocultar la barra lateral", section: "Vista", shortcut: "Ctrl+B", symbol: "panel-left", run: () => sidebarOpen = !sidebarOpen},
            {id: "inspector", title: "Mostrar u ocultar el inspector", section: "Vista", shortcut: "Ctrl+I", symbol: "panel-right", run: () => inspectorOpen = !inspectorOpen},
            {id: "focus", title: "Modo concentración", section: "Vista", shortcut: "Ctrl+Shift+F", symbol: "maximize", run: () => focusMode = !focusMode},
            {id: "zoomin", title: "Ampliar", section: "Vista", shortcut: "Ctrl++", symbol: "zoom-in", run: () => zoom = Math.min(2, Math.round((zoom + 0.1) * 10) / 10)},
            {id: "zoomout", title: "Reducir", section: "Vista", shortcut: "Ctrl+-", symbol: "zoom-out", run: () => zoom = Math.max(0.7, Math.round((zoom - 0.1) * 10) / 10)},
            {id: "zoomreset", title: "Tamaño real", section: "Vista", shortcut: "Ctrl+0", symbol: "search", run: () => zoom = 1},
            {id: "nexttab", title: "Pestaña siguiente", section: "Vista", shortcut: "Ctrl+Tab", symbol: "chevron-right", enabled: tabs.count > 1, run: () => stepTab(1)},
            {id: "prevtab", title: "Pestaña anterior", section: "Vista", shortcut: "Ctrl+Shift+Tab", symbol: "chevron-left", enabled: tabs.count > 1, run: () => stepTab(-1)},
            {id: "closetab", title: "Cerrar la pestaña", section: "Vista", shortcut: "Ctrl+W", symbol: "x", enabled: tabs.count > 0, run: () => closeTab(current)},

            {id: "run", title: "Ejecutar la consulta", section: "Consulta", shortcut: "Ctrl+Shift+Return", symbol: "play", enabled: kind === "query", run: () => pageAct("run")},
            {id: "savequery", title: "Guardar la consulta…", section: "Consulta", symbol: "save", enabled: kind === "query", run: arg => pageAct("save", arg)},
            {id: "builder", title: "Constructor visual", section: "Consulta", symbol: "blocks", enabled: kind === "query", run: () => pageAct("builder")},
            {id: "history", title: "Historial de consultas", section: "Consulta", symbol: "clock", enabled: kind === "query", run: () => pageAct("history")},
            {id: "pdf", title: "Exportar el informe a PDF…", section: "Informe", symbol: "file-text", enabled: kind === "report", run: arg => arg ? pageAct("pdf", arg) : askExport(tabTitle(tab), path => pageAct("pdf", path), ["PDF (*.pdf)"])},
        ]
    }
    /// The commands and, after them, every object of the database to jump to.
    readonly property var paletteEntries: {
        App.db.schemaRevision
        App.db.objectsRevision
        const list = actions.slice()
        if (!hasDb)
            return list
        for (const item of App.db.tables())
            list.push({id: "go", title: item.name, section: item.type === "view" ? "Vistas" : "Tablas", symbol: item.type === "view" ? "eye" : "table", hidden: true, run: () => openObject("table", item.name)})
        for (const kind of ["query", "form", "report"]) {
            for (const item of App.db.objects(kind))
                list.push({id: "go", title: item.name, section: ({query: "Consultas", form: "Formularios", report: "Informes"})[kind], symbol: Theme.objectSymbol(kind), hidden: true, run: () => openObject(kind, item.name, item.id)})
        }
        return list
    }
    function newFromTable(kind, source) {
        const make = name => openObject(kind, "", 0, {source: name})
        if (source) {
            make(source)
            return
        }
        const options = App.db.tableNames(true).map(name => ({label: name, symbol: App.db.isView(name) ? "eye" : "table", value: name}))
        askPick(kind === "form" ? "¿De qué tabla es el formulario?" : "¿De dónde lee el informe?", options, make)
    }
    /// Runs a command by its id. False when there is none, or it is switched off.
    function exec(id, arg) {
        const action = actions.find(a => a.id === id)
        if (!action || action.enabled === false)
            return false
        action.run(arg ?? "")
        return true
    }

    Item {
        Repeater {
            model: win.actions.filter(a => a.shortcut)
            Item {
                required property var modelData
                Shortcut {
                    sequence: parent.modelData.shortcut
                    enabled: !win.busy || parent.modelData.id === "palette"
                    onActivated: win.exec(parent.modelData.id)
                }
            }
        }
        Shortcut {
            sequence: "Escape"
            enabled: win.focusMode && !win.busy
            onActivated: win.focusMode = false
        }
    }

    // --- Dialogs ---------------------------------------------------------------------

    CommandPalette {
        id: palette
        commands: win.paletteEntries
        onChosen: command => command.run("")
    }
    ConfirmDialog {
        id: confirmDialog
    }
    NameDialog {
        id: nameDialog
    }
    PickDialog {
        id: pickDialog
    }
    ImportDialog {
        id: importDialog
        onDone: table => win.openObject("table", table)
    }
    VersionsPanel {
        id: versionsPanel
    }
    FileDialog {
        id: openDialog
        title: "Abrir una base de datos"
        currentFolder: App.fileUrl(App.folder)
        nameFilters: ["Bases de datos SQLite (*.db *.sqlite *.sqlite3 *.db3)", "Todos los archivos (*)"]
        onAccepted: win.openFile(App.localPath(selectedFile))
    }
    FileDialog {
        id: importFile
        title: "Importar un archivo CSV"
        currentFolder: App.fileUrl(App.folder)
        nameFilters: ["Texto separado (*.csv *.tsv *.txt)", "Todos los archivos (*)"]
        onAccepted: importDialog.show(App.localPath(selectedFile))
    }
    FileDialog {
        id: exportDialog
        property var then: null
        title: "Exportar"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            // The filter chosen gives the suffix the name lacks.
            let path = App.localPath(selectedFile)
            if (!/\.\w+$/.test(path))
                path += "." + selectedNameFilter.extensions[0]
            if (then)
                then(path)
        }
    }

    // --- The window ------------------------------------------------------------------

    Rectangle {
        id: bar
        width: parent.width
        height: win.focusMode ? 0 : 46
        visible: !win.focusMode
        color: Theme.chrome
        z: 5

        Row {
            id: barLeft
            x: 10
            height: parent.height
            spacing: 6
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "panel-left"
                checked: win.sidebarOpen
                active: win.hasDb
                onClicked: win.exec("sidebar")
            }
            Item {
                id: titleBox
                function rename() {
                    if (!win.hasDb || App.db.readOnly)
                        return
                    titleField.text = App.db.name
                    titleField.visible = true
                    titleField.selectAll()
                    titleField.forceActiveFocus()
                }
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(Math.max(120, bar.width * 0.2), titleText.implicitWidth + 30)
                height: 28
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: titleArea.containsMouse && win.hasDb ? Theme.hover : "transparent"
                }
                Text {
                    id: titleText
                    x: 8
                    width: parent.width - 30
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !titleField.visible
                    text: win.hasDb ? App.db.name : "Bases"
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // Something is not saved yet.
                Rectangle {
                    x: Math.min(titleText.x + titleText.implicitWidth, parent.width - 22) + 7
                    anchors.verticalCenter: parent.verticalCenter
                    visible: win.dirty && !titleField.visible
                    width: 7
                    height: 7
                    radius: 4
                    color: Theme.tint
                }
                MouseArea {
                    id: titleArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: titleBox.rename()
                }
                Field {
                    id: titleField
                    anchors.fill: parent
                    visible: false
                    font.weight: Font.DemiBold
                    onAccepted: {
                        visible = false
                        if (text.trim() !== "" && text.trim() !== App.db.name)
                            win.renameDatabase(text.trim())
                    }
                    onActiveFocusChanged: if (!activeFocus) visible = false
                    Keys.onEscapePressed: visible = false
                }
            }
        }

        // What this app is for, in the middle.
        Row {
            id: tools
            anchors.centerIn: parent
            // Never over the title nor the buttons on the right.
            anchors.horizontalCenterOffset: Math.max(0, barLeft.x + barLeft.width + 12 - (parent.width - width) / 2)
            height: parent.height
            spacing: 2
            visible: win.hasDb
            component Tool: TButton {
                property string command
                readonly property var action: win.actions.find(a => a.id === command)
                anchors.verticalCenter: parent.verticalCenter
                symbol: action.symbol
                active: action.enabled !== false
                onClicked: win.exec(command)
                ToolTip.visible: hover.hovered
                ToolTip.delay: 600
                ToolTip.text: action.title
                HoverHandler { id: hover }
            }
            component Gap: Item {
                width: 13
                height: parent.height
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Theme.border
                }
            }
            Tool { command: "newtable" }
            Tool { command: "newquery" }
            Tool { command: "relations" }
            Gap {}
            Tool { command: "addrow" }
            Tool { command: "delrow" }
            Tool { command: "addfilter" }
            Gap {}
            Tool { command: "undo" }
            Tool { command: "redo" }
            Gap { visible: bar.width >= 980 }
            Tool { command: "import"; visible: bar.width >= 980 }
            Tool { command: "newreport"; visible: bar.width >= 980 }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            height: parent.height
            spacing: 4
            Rectangle {
                id: commandButton
                anchors.verticalCenter: parent.verticalCenter
                width: bar.width >= 1060 ? 118 : 30
                height: 26
                radius: 6
                color: commandArea.containsMouse ? Theme.sideSelection : (bar.width >= 1060 ? Theme.field : "transparent")
                Sym {
                    x: bar.width >= 1060 ? 9 : 7
                    anchors.verticalCenter: parent.verticalCenter
                    name: "command"
                    size: 15
                    color: Theme.dim
                }
                Text {
                    x: 31
                    anchors.verticalCenter: parent.verticalCenter
                    visible: bar.width >= 1060
                    text: "Órdenes"
                    color: Theme.dim
                    font.pixelSize: 13
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: bar.width >= 1060
                    text: "⌃K"
                    color: Theme.faint
                    font.pixelSize: 11
                }
                MouseArea {
                    id: commandArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: palette.show("")
                }
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "share"
                active: win.hasDb
                onClicked: shareMenu.popup(this, 0, height + 4)
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "panel-right"
                checked: win.inspectorOpen
                active: win.hasDb
                onClicked: win.exec("inspector")
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }
    FMenu {
        id: shareMenu
        FMenuItem {
            text: "Exportar lo que se ve…"
            enabled: !!win.page && !!win.page.exportTo
            onTriggered: win.exec("export")
        }
        FMenuItem {
            text: "Exportar toda la base como SQL…"
            onTriggered: win.exec("dump")
        }
        FMenuItem {
            text: "Exportar el informe a PDF…"
            enabled: !!win.tab && win.tab.kind === "report"
            onTriggered: win.exec("pdf")
        }
        FSeparator {}
        FMenuItem {
            text: "Importar CSV…"
            enabled: !App.db.readOnly
            onTriggered: win.exec("import")
        }
        FSeparator {}
        FMenuItem {
            text: "Versiones…"
            onTriggered: win.exec("versions")
        }
        FMenuItem {
            text: "Copiar la ruta del archivo"
            onTriggered: {
                App.copy(App.db.path)
                win.say("Ruta copiada.")
            }
        }
    }

    Sidebar {
        id: sidebar
        anchors.top: bar.bottom
        anchors.bottom: statusBar.top
        width: win.hasDb && win.sidebarOpen && !win.focusMode ? 228 : 0
        visible: width > 0
        host: win
        currentKey: win.tab ? win.tab.key : ""
    }

    Rectangle {
        id: content
        anchors.top: bar.bottom
        anchors.bottom: statusBar.top
        anchors.left: sidebar.right
        anchors.right: parent.right
        color: Theme.window

        Welcome {
            anchors.fill: parent
            visible: !win.hasDb
            onCreate: templateId => win.newDatabase(templateId)
            onBrowse: openDialog.open()
            onSample: App.openSample()
        }

        // The documents open, as tabs.
        Rectangle {
            id: strip
            width: parent.width
            height: win.hasDb && !win.focusMode ? 34 : 0
            visible: height > 0
            color: Theme.chrome
            clip: true
            ListView {
                id: stripList
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 40
                orientation: ListView.Horizontal
                spacing: 2
                model: tabs
                currentIndex: tabs.currentIndex
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                clip: true
                delegate: Item {
                    id: chip
                    required property int index
                    required property string kind
                    required property string name
                    required property string key
                    readonly property bool on: index === tabs.currentIndex
                    readonly property bool unsaved: {
                        const loader = pages.itemAt(index)
                        return !!loader && !!loader.item && !!loader.item.dirty
                    }
                    width: Math.min(200, Math.max(96, chipText.implicitWidth + 62))
                    height: 34
                    Rectangle {
                        y: 4
                        width: parent.width
                        height: 26
                        radius: 7
                        color: chip.on ? (Theme.dark ? "#3a3a3d" : "#ffffff") : (chipArea.containsMouse ? Theme.hover : "transparent")
                        border.color: chip.on && !Theme.dark ? "#14000000" : "transparent"
                    }
                    Sym {
                        x: 9
                        anchors.verticalCenter: parent.verticalCenter
                        name: Theme.objectSymbol(chip.kind === "table" && chip.name !== "" && App.db.isView(chip.name) ? "view" : chip.kind)
                        size: 13
                        color: chip.on ? Theme.tint : Theme.dim
                    }
                    Text {
                        id: chipText
                        x: 29
                        width: parent.width - 55
                        anchors.verticalCenter: parent.verticalCenter
                        text: win.tabTitle({kind: chip.kind, name: chip.name})
                        color: chip.on ? Theme.text : Theme.dim
                        font.pixelSize: 12
                        font.weight: chip.on ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: chipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.MiddleButton)
                                win.closeTab(chip.index)
                            else
                                tabs.currentIndex = chip.index
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        visible: chip.unsaved && !chipArea.containsMouse && !closeHover.hovered
                        width: 7
                        height: 7
                        radius: 4
                        color: Theme.tint
                    }
                    TButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        visible: chipArea.containsMouse || closeHover.hovered || (chip.on && !chip.unsaved)
                        width: 20
                        height: 20
                        symbol: "x"
                        size: 12
                        tone: Theme.dim
                        onClicked: win.closeTab(chip.index)
                        HoverHandler { id: closeHover }
                    }
                }
            }
            TButton {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                symbol: "plus"
                size: 15
                onClicked: newMenu.popup(this, width - newMenu.width, height + 4)
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
        FMenu {
            id: newMenu
            FMenuItem {
                text: "Tabla nueva"
                enabled: !App.db.readOnly
                onTriggered: win.exec("newtable")
            }
            FMenuItem {
                text: "Consulta nueva"
                onTriggered: win.exec("newquery")
            }
            FMenuItem {
                text: "Formulario nuevo…"
                onTriggered: win.exec("newform")
            }
            FMenuItem {
                text: "Informe nuevo…"
                onTriggered: win.exec("newreport")
            }
            FSeparator {}
            FMenuItem {
                text: "Relaciones"
                onTriggered: win.exec("relations")
            }
        }

        Item {
            id: stage
            anchors.top: strip.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            visible: win.hasDb
            Repeater {
                id: pages
                model: tabs
                Loader {
                    id: slot
                    required property int index
                    required property string kind
                    required property string name
                    required property string key
                    required property int objectId
                    required property string extra
                    // The zoom makes the page smaller and draws it bigger.
                    width: stage.width / win.zoom
                    height: stage.height / win.zoom
                    scale: win.zoom
                    transformOrigin: Item.TopLeft
                    visible: index === tabs.currentIndex
                    sourceComponent: kind === "table" ? tablePage : kind === "query" ? queryPage : kind === "form" ? formPage
                        : kind === "report" ? reportPage : relationsPage
                    onLoaded: {
                        if (extra !== "" && item.apply)
                            item.apply(JSON.parse(extra))
                        if (visible)
                            item.forceActiveFocus()
                    }
                    onVisibleChanged: if (visible && item) item.forceActiveFocus()
                }
            }
            Column {
                anchors.centerIn: parent
                visible: tabs.count === 0
                spacing: 8
                Sym {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "database"
                    size: 34
                    color: Theme.faint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.db.tableNames(true).length === 0 && App.db.schemaRevision >= 0 ? "Esta base aún no tiene tablas" : "Nada abierto"
                    color: Theme.dim
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Crea una tabla con ⌃T, importa un CSV o busca con ⌃K."
                    color: Theme.faint
                    font.pixelSize: 13
                }
                Item { width: 1; height: 4 }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    TextButton {
                        symbol: "table"
                        text: "Tabla nueva"
                        strong: true
                        active: !App.db.readOnly
                        onClicked: win.exec("newtable")
                    }
                    TextButton {
                        symbol: "download"
                        text: "Importar CSV…"
                        color: Theme.field
                        active: !App.db.readOnly
                        onClicked: win.exec("import")
                    }
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
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            width: Math.min(parent.width - 40, toastText.implicitWidth + 32)
            height: 34
            radius: 17
            z: 20
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
                interval: 3600
                onTriggered: toast.opacity = 0
            }
        }
        // The way back out of the focus mode, for who has no keyboard at hand.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            visible: win.focusMode
            width: leave.implicitWidth + 22
            height: 26
            radius: 13
            z: 20
            color: Theme.dark ? "#c0404044" : "#c02c2c2e"
            opacity: leaveArea.containsMouse ? 1 : 0.55
            Text {
                id: leave
                anchors.centerIn: parent
                text: "Esc · salir de concentración"
                color: "#ffffff"
                font.pixelSize: 11
            }
            MouseArea {
                id: leaveArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: win.focusMode = false
            }
        }
    }

    Rectangle {
        id: statusBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: win.focusMode ? 0 : 26
        visible: !win.focusMode
        color: Theme.chrome
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Text {
            id: statusLeft
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.5)
            text: !win.hasDb ? "Sin base de datos" : (win.page && win.page.status ? win.page.status : (App.db.schemaRevision, App.db.tableNames().length) + " tablas")
            color: Theme.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Text {
            anchors.left: statusLeft.right
            anchors.leftMargin: 14
            anchors.right: zoomRow.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: !win.hasDb ? "" : (win.page && win.page.detail ? win.page.detail + "  ·  " : "") + (App.db.readOnly ? "Solo lectura  ·  " : "") + "SQLite"
            color: Theme.faint
            font.pixelSize: 12
            elide: Text.ElideLeft
        }
        Row {
            id: zoomRow
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            TButton {
                width: 22
                height: 20
                symbol: "minus"
                size: 12
                tone: Theme.dim
                active: win.zoom > 0.7
                onClicked: win.exec("zoomout")
            }
            Text {
                width: 42
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(win.zoom * 100) + " %"
                color: Theme.dim
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.exec("zoomreset")
                }
            }
            TButton {
                width: 22
                height: 20
                symbol: "plus"
                size: 12
                tone: Theme.dim
                active: win.zoom < 2
                onClicked: win.exec("zoomin")
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            const path = App.localPath(drop.urls[0])
            if (/\.(csv|tsv|txt)$/i.test(path) && win.hasDb)
                importDialog.show(path)
            else
                win.openFile(path)
        }
    }

    Component {
        id: tablePage
        TablePage {
            host: win
            key: parent.key
            name: parent.name
        }
    }
    Component {
        id: queryPage
        QueryPage {
            host: win
            key: parent.key
            name: parent.name
            objectId: parent.objectId
        }
    }
    Component {
        id: formPage
        FormPage {
            host: win
            key: parent.key
            name: parent.name
            objectId: parent.objectId
        }
    }
    Component {
        id: reportPage
        ReportPage {
            host: win
            key: parent.key
            name: parent.name
            objectId: parent.objectId
        }
    }
    Component {
        id: relationsPage
        RelationsPage {
            host: win
            key: parent.key
        }
    }

    // --- Tests: what --do can ask for ---------------------------------------------------

    /// A command by its id, or one of the verbs only a test needs.
    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        switch (verb) {
        case "table":
            openObject("table", arg)
            return
        case "query": {
            const saved = App.db.objectNamed("query", arg)
            openObject("query", saved.name ?? "", saved.id ?? 0)
            return
        }
        case "formobject": {
            const saved = App.db.objectNamed("form", arg)
            openObject("form", saved.name ?? "", saved.id ?? 0)
            return
        }
        case "report": {
            const saved = App.db.objectNamed("report", arg)
            openObject("report", saved.name ?? "", saved.id ?? 0)
            return
        }
        case "tab":
            tabs.currentIndex = Number(arg)
            return
        case "template":
            App.createPath(App.freePath(arg), arg)
            return
        case "blank":
            App.createPath(App.freePath(arg || "Sin título"), "")
            return
        case "big":
            App.makeBig("grande", Number(arg) || 300000)
            openObject("table", "grande")
            return
        case "say":
            say(arg)
            return
        case "side":
            sideFilter = arg
            return
        case "sharemenu":
            shareMenu.popup(content, content.width - 240, 4)
            return
        case "newmenu":
            newMenu.popup(strip, strip.width - 220, strip.height)
            return
        case "confirm":
            dropObject("table", arg, 0)
            return
        case "yes":
            confirmDialog.accept()
            return
        case "sidemenu":
            sidebar.menuFor(arg)
            return
        case "version":
            versionsPanel.act(arg)
            return
        case "importgo":
            importDialog.act(arg)
            return
        }
        if (exec(verb, arg))
            return
        if (!pageAct(verb, arg))
            console.warn("--do: no such action", action)
    }
    Timer {
        id: testStep
        property int at: 0
        interval: 140
        repeat: true
        onTriggered: {
            // One at a time: what a step opens is there for the next.
            if (at < App.testActions.length) {
                win.runAction(App.testActions[at++].trim())
                return
            }
            stop()
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 700
        onTriggered: App.saveShot(win)
    }
}
