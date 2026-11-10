import QtQuick
import QtQuick.Controls.Basic
import WonderlandBases

/// A query: SQL typed (or built by ticking columns), run, and its rows below.
Item {
    id: page

    property var host
    property string key
    property string name
    property int objectId
    /// sql or builder.
    property string mode: "sql"
    property string savedText
    /// What the builder holds: {tables, columns: [{table, column, show, criterion, sort, total}], distinct, limit}
    property var design: ({tables: [], columns: [], distinct: false, limit: 0})
    property bool historyOpen: false
    /// The last run: {ok, error, statement, affected, statements, elapsed}
    property var outcome: ({})
    property bool ran: false
    /// What was last answered for each parameter.
    property var known: ({})
    readonly property bool dirty: editor.text !== savedText && editor.text.trim() !== ""
    readonly property string status: {
        if (!ran)
            return "Consulta sin ejecutar"
        if (!outcome.ok)
            return "Error en la consulta"
        const n = grid.count
        const time = " · " + Math.max(1, Math.round(outcome.elapsed ?? 0)) + " ms"
        if (grid.sql !== "")
            return (n === 1 ? "1 fila" : host.count(n) + " filas") + time
        return (outcome.affected === 1 ? "1 fila cambiada" : host.count(outcome.affected ?? 0) + " filas cambiadas") + time
    }
    readonly property string detail: mode === "builder" ? "Constructor" : "SQL"
    readonly property var exportFilters: ["CSV (*.csv)", "JSON (*.json)", "SQL (*.sql)", "Texto con tabuladores (*.tsv)"]
    readonly property var totals: [{id: "", label: "—"}, {id: "group", label: "Agrupar"}, {id: "sum", label: "Suma"}, {id: "count", label: "Cuenta"},
        {id: "avg", label: "Media"}, {id: "min", label: "Mínimo"}, {id: "max", label: "Máximo"}]
    readonly property var sorts: [{id: "", label: "—"}, {id: "asc", label: "A → Z"}, {id: "desc", label: "Z → A"}]

    Component.onCompleted: {
        if (objectId > 0) {
            const def = App.db.object(objectId).definition ?? {}
            editor.text = def.sql ?? ""
            savedText = editor.text
            if (def.design && def.design.tables)
                design = def.design
        }
    }

    function exportTo(path) {
        if (grid.sql === "") {
            host.say("Ejecuta antes una consulta que devuelva filas.")
            return
        }
        const error = App.exportRows(grid.model.viewSql(), name || "consulta", path)
        host.say(error !== "" ? error : "Exportado a " + path)
    }

    // --- Running ------------------------------------------------------------------------

    function run() {
        const sql = editor.text.trim()
        if (sql === "")
            return
        const names = App.parameters(sql)
        if (names.length > 0 && names.some(n => known[n] === undefined)) {
            params.ask(names, known, values => {
                known = values
                execute(sql, values)
            })
            return
        }
        execute(sql, known)
    }
    function execute(sql, binds) {
        const parts = App.statements(sql)
        const last = parts.length > 0 ? parts[parts.length - 1] : sql
        // A lone SELECT is only looked at here: the grid reads it page by page.
        const lone = parts.length <= 1 && App.selects(last)
        const result = App.db.run(sql, binds, lone ? 1 : 1000)
        outcome = result
        ran = true
        App.db.addHistory(sql, !!result.ok)
        completer.close()
        if (!result.ok) {
            grid.sql = ""
            return
        }
        if (App.selects(last)) {
            grid.parameters = binds
            if (grid.sql === last)
                grid.model.refresh()
            else
                grid.sql = last
        } else {
            grid.sql = ""
        }
    }
    function save(given) {
        const store = title => {
            const id = App.db.saveObject("query", title, {sql: editor.text, design: mode === "builder" || design.tables.length > 0 ? design : undefined}, objectId)
            if (id === 0) {
                host.say(App.db.lastError || "No se pudo guardar la consulta.")
                return
            }
            savedText = editor.text
            host.objectSaved(key, "query", id, title)
        }
        if (given)
            store(given)
        else if (objectId > 0)
            store(name)
        else
            host.askName("Guardar la consulta", "Aparecerá en la barra lateral, bajo Consultas.", "", "Guardar", store)
    }

    // --- The builder --------------------------------------------------------------------

    function rebuild(next) {
        design = next
        editor.text = App.builderSql(next)
    }
    function toggleTable(table) {
        const next = JSON.parse(JSON.stringify(design))
        if (next.tables.includes(table)) {
            next.tables = next.tables.filter(t => t !== table)
            next.columns = next.columns.filter(c => c.table !== table)
        } else {
            next.tables.push(table)
            for (const column of App.db.columnNames(table))
                next.columns.push({table: table, column: column, alias: "", show: true, criterion: "", sort: "", total: ""})
        }
        rebuild(next)
    }
    /// `where` is table.column; sets one of show, criterion, sort, total.
    function setColumn(where, what, value) {
        const dot = where.indexOf(".")
        const next = JSON.parse(JSON.stringify(design))
        const column = next.columns.find(c => c.table === where.slice(0, dot) && c.column === where.slice(dot + 1))
        if (!column)
            return false
        column[what] = value
        rebuild(next)
        return true
    }

    // --- Completion -----------------------------------------------------------------------

    property var completion: ({start: 0, prefix: "", items: []})
    function suggest(force) {
        const found = App.complete(editor.text, editor.cursorPosition)
        completion = found
        const items = found.items ?? []
        if (items.length === 0 || (!force && (found.prefix ?? "").length < 2) || (items.length === 1 && items[0].text === found.prefix)) {
            completer.close()
            return
        }
        choices.currentIndex = 0
        const at = editor.cursorRectangle
        const spot = editor.mapToItem(page, at.x, at.y + at.height)
        completer.x = Math.min(spot.x, page.width - completer.width - 8)
        completer.y = spot.y + 2
        completer.open()
    }
    function acceptCompletion() {
        const item = completion.items[choices.currentIndex]
        if (!item)
            return
        const end = editor.cursorPosition
        editor.remove(completion.start, end)
        editor.insert(completion.start, item.text)
        completer.close()
    }

    function act(verb, arg) {
        switch (verb) {
        case "run":
            run()
            return true
        case "save":
            save(arg)
            return true
        case "text":
            editor.text = arg
            editor.cursorPosition = editor.length
            return true
        case "append":
            editor.text += arg
            editor.cursorPosition = editor.length
            return true
        case "param": {
            const at = arg.indexOf("=")
            const next = Object.assign({}, known)
            next[arg.slice(0, at)] = arg.slice(at + 1)
            known = next
            return true
        }
        case "builder":
            mode = mode === "builder" && arg === "" ? "sql" : "builder"
            return true
        case "sql":
            mode = "sql"
            return true
        case "history":
            historyOpen = !historyOpen
            return true
        case "addtable":
            toggleTable(arg)
            return true
        case "criterion":
        case "total":
        case "sort": {
            const at = arg.indexOf("=")
            return setColumn(arg.slice(0, at), verb, arg.slice(at + 1))
        }
        case "show": {
            const at = arg.indexOf("=")
            return setColumn(arg.slice(0, at), "show", arg.slice(at + 1) !== "0")
        }
        case "complete":
            suggest(true)
            return true
        case "accept":
            acceptCompletion()
            return true
        case "refresh":
            if (ran)
                run()
            return true
        case "export":
            exportTo(arg)
            return true
        }
        return false
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 42
        z: 4
        color: Theme.window
        Row {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Segmented {
                options: [{key: "sql", label: "SQL"}, {key: "builder", label: "Constructor"}]
                current: page.mode
                onPicked: key => page.mode = key
            }
            TextButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "play"
                text: "Ejecutar"
                strong: true
                onClicked: page.run()
            }
            TextButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "save"
                text: page.objectId > 0 ? "Guardar" : "Guardar…"
                active: editor.text.trim() !== ""
                onClicked: page.save("")
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: bar.width > 700
                text: "⌃⇧↵ ejecuta · ⌃Espacio completa"
                color: Theme.faint
                font.pixelSize: 11
                rightPadding: 6
            }
            TButton {
                symbol: "clock"
                checked: page.historyOpen
                onClicked: page.historyOpen = !page.historyOpen
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // The builder: tables on the left, their columns on the right.
    Rectangle {
        id: builder
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: history.visible ? history.left : parent.right
        height: page.mode === "builder" ? Math.max(170, Math.min(300, (page.height - bar.height) * 0.42)) : 0
        visible: height > 0
        color: Theme.window
        clip: true
        ListView {
            id: tableList
            x: 8
            y: 8
            width: 190
            height: parent.height - 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: {
                App.db.schemaRevision
                return App.db.tableNames(true)
            }
            header: Caption {
                x: 6
                height: 22
                text: "Tablas"
            }
            delegate: Item {
                required property string modelData
                width: tableList.width
                height: 26
                Check {
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 12
                    text: modelData
                    checked: page.design.tables.includes(modelData)
                    onToggled: page.toggleTable(modelData)
                }
            }
        }
        Rectangle {
            x: 206
            width: 1
            height: parent.height
            color: Theme.line
        }
        Item {
            id: columnsPane
            x: 207
            width: parent.width - 207
            height: parent.height
            readonly property int nameWidth: Math.max(150, width - 60 - 110 - 100 - 190 - 40)
            Row {
                id: heads
                x: 14
                y: 8
                height: 22
                spacing: 8
                Caption { width: 44; text: "Ver" }
                Caption { width: columnsPane.nameWidth; text: "Columna" }
                Caption { width: 110; text: "Total" }
                Caption { width: 100; text: "Orden" }
                Caption { width: 170; text: "Criterio" }
            }
            ListView {
                id: columnList
                anchors.fill: parent
                anchors.topMargin: 32
                anchors.bottomMargin: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.design.columns
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: line
                    required property var modelData
                    readonly property string where: modelData.table + "." + modelData.column
                    width: columnList.width
                    height: 32
                    Row {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Item {
                            width: 44
                            height: 28
                            Check {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !!line.modelData.show
                                onToggled: on => page.setColumn(line.where, "show", on)
                            }
                        }
                        Text {
                            width: columnsPane.nameWidth
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.StyledText
                            text: "<font color=\"" + Theme.dim + "\">" + line.modelData.table + ".</font>" + line.modelData.column
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        FCombo {
                            width: 110
                            model: page.totals.map(t => t.label)
                            currentIndex: Math.max(0, page.totals.findIndex(t => t.id === (line.modelData.total ?? "")))
                            onActivated: index => page.setColumn(line.where, "total", page.totals[index].id)
                        }
                        FCombo {
                            width: 100
                            model: page.sorts.map(t => t.label)
                            currentIndex: Math.max(0, page.sorts.findIndex(t => t.id === (line.modelData.sort ?? "")))
                            onActivated: index => page.setColumn(line.where, "sort", page.sorts[index].id)
                        }
                        Field {
                            width: 170
                            text: line.modelData.criterion ?? ""
                            placeholderText: "> 10, Madrid, a*…"
                            onEditingFinished: {
                                if (text !== (line.modelData.criterion ?? ""))
                                    page.setColumn(line.where, "criterion", text)
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: page.design.columns.length === 0
                text: "Marca las tablas de la izquierda: las que tengan claves foráneas entre sí se unen solas."
                width: Math.min(parent.width - 40, 380)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.faint
                font.pixelSize: 13
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // The statement.
    Rectangle {
        id: editorBox
        anchors.top: builder.bottom
        anchors.left: parent.left
        anchors.right: history.visible ? history.left : parent.right
        height: page.mode === "builder" ? Math.min(132, Math.max(80, page.height * 0.2)) : Math.max(110, Math.min(260, (page.height - bar.height) * 0.36))
        color: Theme.dark ? "#1c1c1e" : "#ffffff"
        Flickable {
            id: scroller
            anchors.fill: parent
            contentWidth: width
            contentHeight: editor.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            function reveal(r) {
                if (contentY >= r.y)
                    contentY = r.y
                else if (contentY + height <= r.y + r.height)
                    contentY = r.y + r.height - height
            }
            TextEdit {
                id: editor
                width: scroller.width
                height: Math.max(implicitHeight, scroller.height)
                padding: 12
                leftPadding: 14
                readOnly: page.mode === "builder"
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: "#ffffff"
                font.family: Theme.mono
                font.pixelSize: 13
                tabStopDistance: 28
                onCursorRectangleChanged: scroller.reveal(cursorRectangle)
                onTextChanged: {
                    if (activeFocus && !readOnly)
                        page.suggest(false)
                }
                Keys.onPressed: event => {
                    const ctrl = event.modifiers & Qt.ControlModifier
                    if (completer.visible) {
                        if (event.key === Qt.Key_Down) {
                            choices.currentIndex = Math.min(choices.count - 1, choices.currentIndex + 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up) {
                            choices.currentIndex = Math.max(0, choices.currentIndex - 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            page.acceptCompletion()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            completer.close()
                            event.accepted = true
                        }
                    } else if (ctrl && event.key === Qt.Key_Space) {
                        page.suggest(true)
                        event.accepted = true
                    }
                }
                Text {
                    x: editor.leftPadding
                    y: editor.topPadding
                    visible: editor.text === ""
                    text: page.mode === "builder" ? "Aquí aparece el SQL de lo que marques arriba" : "SELECT … FROM …   (los parámetros se escriben :así)"
                    color: Theme.faint
                    font: editor.font
                }
            }
        }
        Highlighter {
            document: editor.textDocument
            dark: Theme.dark
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // What came of it.
    Item {
        id: results
        anchors.top: editorBox.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: history.visible ? history.left : parent.right
        Rectangle {
            anchors.fill: parent
            color: Theme.window
        }
        DataGrid {
            id: grid
            anchors.fill: parent
            visible: page.ran && !!page.outcome.ok && sql !== ""
            compact: true
            onSaid: message => page.host.say(message)
        }
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 60, 520)
            visible: !grid.visible
            spacing: 8
            Sym {
                anchors.horizontalCenter: parent.horizontalCenter
                name: !page.ran ? "play" : (page.outcome.ok ? "check" : "info")
                size: 26
                color: page.ran && !page.outcome.ok ? Theme.tint : Theme.faint
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: {
                    if (!page.ran)
                        return "Escribe una consulta y ejecútala"
                    if (!page.outcome.ok)
                        return page.outcome.error || "La consulta ha fallado"
                    const n = page.outcome.statements ?? 1
                    return (n === 1 ? "Sentencia ejecutada" : n + " sentencias ejecutadas") + " · " + (page.outcome.affected === 1 ? "1 fila cambiada" : (page.outcome.affected ?? 0) + " filas cambiadas")
                }
                color: page.ran && !page.outcome.ok ? Theme.text : Theme.dim
                font.pixelSize: 14
                font.weight: page.ran && !page.outcome.ok ? Font.DemiBold : Font.Normal
            }
            Text {
                width: parent.width
                visible: page.ran && !page.outcome.ok && !!page.outcome.statement
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                text: page.outcome.statement ?? ""
                color: Theme.dim
                font.family: Theme.mono
                font.pixelSize: 12
            }
            Text {
                width: parent.width
                visible: page.ran && !page.outcome.ok
                horizontalAlignment: Text.AlignHCenter
                text: "Nada ha cambiado: lo que hizo la consulta antes de fallar se ha deshecho."
                wrapMode: Text.Wrap
                color: Theme.faint
                font.pixelSize: 12
            }
        }
    }

    // What was run before.
    Rectangle {
        id: history
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 264
        visible: page.historyOpen
        color: Theme.chrome
        Rectangle {
            width: 1
            height: parent.height
            color: Theme.border
        }
        Text {
            x: 16
            y: 12
            text: "Historial"
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        TButton {
            anchors.right: parent.right
            anchors.rightMargin: 8
            y: 5
            symbol: "trash-2"
            size: 14
            tone: Theme.dim
            onClicked: {
                App.db.clearHistory()
                page.outcomeChanged()
            }
        }
        ListView {
            id: past
            anchors.fill: parent
            anchors.topMargin: 40
            anchors.leftMargin: 9
            anchors.rightMargin: 8
            anchors.bottomMargin: 8
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            model: {
                page.outcome
                return page.historyOpen ? App.db.history(60) : []
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry
                required property var modelData
                width: past.width
                height: 50
                radius: 7
                color: pastArea.containsMouse ? Theme.hover : "transparent"
                Rectangle {
                    x: 8
                    y: 10
                    width: 6
                    height: 6
                    radius: 3
                    color: entry.modelData.ok ? "#34a853" : Theme.tint
                }
                Text {
                    x: 22
                    y: 5
                    width: parent.width - 30
                    text: String(entry.modelData.sql).replace(/\s+/g, " ")
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: 11
                    maximumLineCount: 2
                    wrapMode: Text.WrapAnywhere
                    elide: Text.ElideRight
                }
                Text {
                    x: 22
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: String(entry.modelData.ran).replace("T", " ").slice(0, 16)
                    color: Theme.faint
                    font.pixelSize: 10
                }
                MouseArea {
                    id: pastArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        page.mode = "sql"
                        editor.text = entry.modelData.sql
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: past.count === 0
                text: "Aún no se ha ejecutado nada"
                color: Theme.faint
                font.pixelSize: 12
            }
        }
    }

    Popup {
        id: completer
        width: 250
        height: Math.min(choices.contentHeight + 10, 200)
        padding: 5
        focus: false
        closePolicy: Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 8
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: ListView {
            id: choices
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.completion.items ?? []
            delegate: Rectangle {
                id: choice
                required property var modelData
                required property int index
                readonly property bool on: index === choices.currentIndex
                width: choices.width
                height: 24
                radius: 5
                color: on ? Theme.accent : "transparent"
                Sym {
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    name: choice.modelData.kind === "table" ? "table" : (choice.modelData.kind === "column" ? "columns-3" : "code")
                    size: 12
                    color: choice.on ? "#ffffff" : Theme.faint
                }
                Text {
                    x: 26
                    width: parent.width - 32
                    anchors.verticalCenter: parent.verticalCenter
                    text: choice.modelData.text
                    color: choice.on ? "#ffffff" : Theme.text
                    font.family: Theme.mono
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        choices.currentIndex = choice.index
                        page.acceptCompletion()
                    }
                }
            }
        }
    }
    ParamsDialog {
        id: params
        parent: Overlay.overlay
    }
}
