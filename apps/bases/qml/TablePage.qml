import QtQuick
import QtQuick.Controls.Basic

/// A table or a view. The same rows read as a grid, a gallery of cards, a
/// board and a form; and the design of the table.
Item {
    id: page

    property var host
    property string key
    /// Empty for a table that is being made.
    property string name
    /// grid, cards, board, form or design.
    property string mode: "grid"
    /// The column the lanes of the board come from.
    property string boardColumn
    readonly property bool isView: {
        App.db.schemaRevision
        return name !== "" && App.db.isView(name)
    }
    readonly property bool dirty: mode === "design" && design.dirty
    readonly property bool inspectorOpen: host ? host.inspectorOpen : false
    readonly property bool editable: grid.editable
    readonly property var rows: grid.model
    readonly property string titleColumn: {
        App.db.schemaRevision
        return name === "" ? "" : App.db.labelColumn(name)
    }
    /// What the window's status bar says.
    readonly property string status: {
        if (mode === "design")
            return design.dirty ? "Diseño con cambios sin guardar" : "Diseño de la tabla"
        const n = rows.count
        const all = rows.total
        if (rows.error !== "" && n > 0)
            return rows.error
        let said = n === 1 ? "1 registro" : host.count(n) + " registros"
        if (mode !== "board" && grid.curRow >= 0 && n > 0) {
            const many = mode === "grid" ? grid.selectedRows().length : 1
            said = many > 1 ? host.count(many) + " seleccionados de " + host.count(n) : "Registro " + host.count(grid.curRow + 1) + " de " + host.count(n)
        }
        if (rows.filtered && all >= 0)
            said += " · filtrado de " + host.count(all)
        if (!rows.editable)
            said += App.db.readOnly ? "" : " · vista: no se edita"
        return said
    }
    readonly property string detail: {
        if (mode !== "grid" || !grid.field)
            return ""
        const f = grid.field
        return f.name + " · " + (f.type || "sin tipo") + (f.pk > 0 ? " · clave" : "") + (f.fkTable ? " → " + f.fkTable : "")
    }
    readonly property var views: [
        {key: "grid", label: "Rejilla"}, {key: "cards", label: "Tarjetas"}, {key: "board", label: "Tablero"}, {key: "form", label: "Formulario"}]

    // --- What each view remembers ------------------------------------------------------

    property bool restored: false
    function restore() {
        restored = false
        const saved = name === "" ? ({}) : App.db.objectNamed("layout", name)
        const def = saved.definition ?? {}
        if (def.view && def.view !== "design" && mode !== "design")
            mode = def.view
        boardColumn = def.boardColumn ?? ""
        if (def.conditions && def.conditions.length > 0 && rows.conditions.length === 0)
            rows.conditions = def.conditions
        restored = true
    }
    function remember() {
        if (!restored || name === "" || App.db.readOnly || mode === "design")
            return
        const saved = App.db.objectNamed("layout", name)
        const def = {view: mode, boardColumn: boardColumn, conditions: rows.conditions}
        if (JSON.stringify(saved.definition ?? {}) !== JSON.stringify(def))
            App.db.saveObject("layout", name, def, saved.id ?? 0)
    }
    /// The first column worth a board: a foreign key, a box, or a text of few values.
    function guessBoardColumn() {
        const fields = rows.fields
        const pick = fields.find(f => f.fkTable) ?? fields.find(f => f.kind === "bool")
            ?? fields.find(f => f.kind === "text" && f.name !== titleColumn && !(f.pk > 0))
        return pick ? pick.name : (fields.length > 0 ? fields[0].name : "")
    }

    Component.onCompleted: {
        if (name === "")
            mode = "design"
        design.load(isView ? "" : name)
        restore()
    }
    onModeChanged: {
        if (mode === "design" && !design.dirty)
            design.load(name)
        if (mode === "board" && (boardColumn === "" || !rows.fields.some(f => f.name === boardColumn)))
            boardColumn = guessBoardColumn()
        if (mode === "cards")
            cards.reveal()
        remember()
    }
    onBoardColumnChanged: remember()

    /// What Main hands over when it opens the tab: {view, conditions}.
    function apply(extra) {
        if (extra.view)
            mode = extra.view
        if (extra.conditions) {
            rows.conditions = extra.conditions
            remember()
        }
    }
    readonly property var exportFilters: ["CSV (*.csv)", "JSON (*.json)", "SQL (*.sql)", "Texto con tabuladores (*.tsv)"]
    function exportTo(path) {
        // What is in sight when filters are on, the whole table otherwise.
        const error = App.exportRows(rows.filtered || rows.sortColumn >= 0 ? rows.viewSql() : name, name, path)
        host.say(error !== "" ? error : "Exportado a " + path)
    }
    function duplicate() {
        if (!editable || grid.curRow < 0)
            return
        const values = rows.record(grid.curRow)
        for (const f of rows.fields) {
            if (f.pk > 0 || f.generated || values[f.name] === null || values[f.name] === undefined)
                delete values[f.name]
        }
        const at = rows.insert(values)
        if (at === -2)
            host.say(rows.error)
        else
            grid.landed(at)
    }
    function follow(table, column, value) {
        host.openObject("table", table, 0, {view: "grid", conditions: [{column: column, op: "eq", value: String(value)}]})
    }
    function act(action, arg) {
        switch (action) {
        case "view": {
            const [view, column] = arg.split(":")
            if (view === "board" && column)
                boardColumn = column
            mode = view === "data" ? "grid" : view
            return true
        }
        case "data":
            mode = "grid"
            return true
        case "design":
            mode = "design"
            return true
        case "pill": {
            // column,op,value — or nothing, to compose one by hand.
            if (mode === "design")
                return true
            const parts = arg.split(",")
            if (parts.length >= 2)
                pills.add(parts[0], parts[1], parts.slice(2).join(","))
            else
                pills.compose(arg || (grid.field ? grid.field.name : ""))
            return true
        }
        case "unpill":
            pills.removeAt(Number(arg))
            return true
        case "filter":
            grid.filtering = true
            if (arg) {
                const at = arg.indexOf("=")
                rows.setFilter(arg.slice(0, at), arg.slice(at + 1))
            }
            return true
        case "clear":
            search.text = ""
            grid.clearFilters()
            remember()
            return true
        case "sort":
            rows.sortBy(rows.fields.findIndex(f => f.name === arg))
            return true
        case "search":
            if (arg === "" && host && !App.testing) {
                search.forceActiveFocus()
                search.selectAll()
                return true
            }
            search.text = arg
            grid.search = arg
            return true
        case "cell": {
            const [row, column] = arg.split(",").map(Number)
            grid.select(row, column, false)
            return true
        }
        case "rows": {
            const [from, to] = arg.split(",").map(Number)
            grid.selectRow(from, false)
            grid.selectRow(to, true)
            return true
        }
        case "edit":
            grid.edit(arg === "" ? undefined : arg)
            return true
        case "commit":
            grid.commit()
            return true
        case "type":
            grid.edit(arg)
            grid.commit()
            return true
        case "null":
            grid.setNull()
            return true
        case "toggle":
            grid.toggle()
            return true
        case "addrow":
            grid.addRow()
            return true
        case "delrow":
            grid.removeSelected()
            return true
        case "duprow":
            duplicate()
            return true
        case "refresh":
            rows.refresh()
            return true
        case "put": {
            const at = arg.indexOf("=")
            return form.put(arg.slice(0, at), arg.slice(at + 1))
        }
        case "movecard": {
            const [from, at, to] = arg.split(",").map(Number)
            return board.moveCard(from, at, to)
        }
        case "field":
            design.picked = Number(arg)
            return true
        case "addfield":
            design.add()
            if (arg)
                design.set(design.picked, "name", arg)
            design.shape++
            return true
        case "delfield":
            design.remove(Number(arg))
            return true
        case "movefield": {
            const [index, by] = arg.split(",").map(Number)
            design.move(index, by)
            return true
        }
        case "tablename":
            design.def.name = arg
            design.touch(true)
            return true
        case "savetable":
            design.save()
            return true
        case "export":
            exportTo(arg)
            return true
        }
        return false
    }
    function save() {
        if (mode === "design")
            design.save()
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 42
        color: Theme.window
        z: 4
        Row {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Segmented {
                visible: page.name !== ""
                options: page.views
                current: page.mode
                onPicked: key => page.mode = key
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.name !== "" && !page.isView
                symbol: "columns-3"
                checked: page.mode === "design"
                onClicked: page.mode = page.mode === "design" ? "grid" : "design"
                ToolTip.visible: designHover.hovered
                ToolTip.delay: 600
                ToolTip.text: "Diseño de la tabla"
                HoverHandler { id: designHover }
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.mode === "board"
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Agrupar por"
                    color: Theme.dim
                    font.pixelSize: 12
                    leftPadding: 4
                }
                FCombo {
                    width: 150
                    model: page.rows.fields.map(f => f.name)
                    currentIndex: page.rows.fields.findIndex(f => f.name === page.boardColumn)
                    onActivated: index => page.boardColumn = page.rows.fields[index].name
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: page.mode !== "design"
            spacing: 6
            Field {
                id: search
                width: Math.max(110, Math.min(220, bar.width - 640))
                placeholderText: "Buscar en la tabla"
                leftPadding: 28
                onTextEdited: searchLater.restart()
                onAccepted: grid.search = text
                Sym {
                    x: 9
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 13
                    color: Theme.faint
                }
                Timer {
                    id: searchLater
                    interval: 300
                    onTriggered: grid.search = search.text
                }
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "plus"
                active: page.editable
                onClicked: grid.addRow()
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "refresh-cw"
                size: 15
                onClicked: page.rows.refresh()
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // The filters, chained.
    Rectangle {
        id: pillBar
        anchors.top: bar.bottom
        width: parent.width
        height: page.mode === "design" ? 0 : 36
        visible: height > 0
        color: Theme.window
        z: 3
        FilterBar {
            id: pills
            anchors.fill: parent
            model: page.rows
            onChanged: page.remember()
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.line
        }
    }

    Item {
        id: body
        anchors.top: pillBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: recordSide.visible ? recordSide.left : parent.right
        visible: page.mode !== "design"

        Rectangle {
            anchors.fill: parent
            color: Theme.window
        }
        DataGrid {
            id: grid
            anchors.fill: parent
            visible: page.mode === "grid"
            focus: visible
            table: page.name
            // The window's status bar says it.
            compact: true
            onSaid: message => page.host.say(message)
        }
        CardsView {
            id: cards
            anchors.fill: parent
            visible: page.mode === "cards"
            model: page.rows
            current: grid.curRow
            titleColumn: page.titleColumn
            onPicked: row => grid.select(row, grid.curCol, false)
            onOpened: row => {
                grid.select(row, grid.curCol, false)
                page.mode = "form"
            }
        }
        BoardView {
            id: board
            anchors.fill: parent
            visible: page.mode === "board"
            model: visible ? page.rows : null
            column: page.boardColumn
            titleColumn: page.titleColumn
            property var pickedRecord: ({})
            onPicked: (key, record) => {
                currentKey = JSON.stringify(key)
                pickedRecord = record
            }
            onSaid: message => page.host.say(message)
        }
        RecordForm {
            id: form
            anchors.fill: parent
            visible: page.mode === "form"
            model: page.rows
            current: grid.curRow
            onMoved: row => grid.select(row, grid.curCol, false)
            onSaid: message => page.host.say(message)
            onAdded: grid.addRow()
        }

        // What is most likely wanted of the cell picked, floating by the grid.
        SelectionBar {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            z: 6
            visible: page.mode === "grid" && grid.curRow >= 0 && !grid.editing && page.rows.count > 0 && !(page.host && page.host.focusMode)
            entries: [
                {symbol: "list-filter", tip: "Filtrar por este valor", verb: "same"},
                {symbol: "arrow-up-down", tip: "Ordenar por esta columna", verb: "sort"},
                {symbol: "copy", tip: "Copiar el valor", verb: "copy"},
                {symbol: "pencil", tip: "Editar la celda", verb: "edit", active: page.editable},
                {symbol: "square", tip: "Vaciar (NULL)", verb: "null", active: page.editable},
                {symbol: "layout-list", tip: "Ver como formulario", verb: "form"},
                {symbol: "file-plus", tip: "Duplicar el registro", verb: "dup", active: page.editable},
                {symbol: "trash-2", tip: "Eliminar el registro", verb: "del", active: page.editable},
            ]
            onChosen: verb => {
                switch (verb) {
                case "same": {
                    const cell = page.rows.value(grid.curRow, grid.curCol)
                    if (cell === null || cell === undefined)
                        pills.add(grid.field.name, "null", "")
                    else
                        pills.add(grid.field.name, "eq", page.rows.text(grid.curRow, grid.curCol))
                    break
                }
                case "sort":
                    page.rows.sortBy(grid.curCol)
                    break
                case "copy":
                    App.copy(page.rows.text(grid.curRow, grid.curCol))
                    page.host.say("Valor copiado.")
                    break
                case "edit":
                    grid.edit()
                    break
                case "null":
                    grid.setNull()
                    break
                case "form":
                    page.mode = "form"
                    break
                case "dup":
                    page.duplicate()
                    break
                case "del":
                    grid.removeSelected()
                    break
                }
            }
        }
    }

    // The record picked and what it is related to.
    RecordPanel {
        id: recordSide
        anchors.top: pillBar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: page.mode !== "design" && page.inspectorOpen
        table: page.name
        fields: page.rows.fields
        activeColumn: page.mode === "grid" && grid.field ? grid.field.name : ""
        heading: page.mode === "board" ? "Tarjeta" : (grid.curRow >= 0 ? "Registro " + grid.fmt(grid.curRow + 1) : "Registro")
        record: {
            App.db.dataRevision
            page.rows.count
            if (page.mode === "board")
                return board.pickedRecord
            return grid.curRow >= 0 ? page.rows.record(grid.curRow) : ({})
        }
        onFollow: (table, column, value) => page.follow(table, column, value)
    }

    DesignView {
        id: design
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        visible: page.mode === "design"
        inspectorOpen: page.inspectorOpen
        onSaid: message => page.host.say(message)
        onConfirm: (title, text, action, callback) => page.host.confirm(title, text, action, callback)
        onSaved: name => {
            const was = page.name
            page.name = name
            page.host.tableSaved(page.key, was, name)
            page.restore()
        }
    }
}
