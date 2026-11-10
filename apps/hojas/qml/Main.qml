import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

ApplicationWindow {
    id: win

    readonly property var book: App.book
    /// The cell the keyboard is on, and the one the selection hangs from.
    property int curRow: 0
    property int curCol: 0
    property int ancRow: 0
    property int ancCol: 0
    readonly property var selection: {
        book.revision
        let a = {r1: Math.min(curRow, ancRow), c1: Math.min(curCol, ancCol), r2: Math.max(curRow, ancRow), c2: Math.max(curCol, ancCol)}
        if (!book.open)
            return a
        // A merge is taken whole.
        const first = book.extent(a.r1, a.c1)
        const last = book.extent(a.r2, a.c2)
        return {r1: Math.min(a.r1, first.r1), c1: Math.min(a.c1, first.c1), r2: Math.max(a.r2, last.r2), c2: Math.max(a.c2, last.c2)}
    }
    readonly property bool single: {
        if (!book.open)
            return true
        const e = book.extent(curRow, curCol)
        return selection.r1 === e.r1 && selection.c1 === e.c1 && selection.r2 === e.r2 && selection.c2 === e.c2
    }
    readonly property var info: {
        book.revision
        return book.open ? book.info(curRow, curCol) : ({})
    }
    readonly property var stats: {
        book.revision
        return book.open && !single ? book.stats(selection.r1, selection.c1, selection.r2, selection.c2) : ({})
    }
    readonly property var table: {
        book.revision
        return book.open ? book.tableAt(curRow, curCol) : ({})
    }

    // --- Typing in a cell
    property bool editing: false
    property string editText: ""
    property int editCursor: 0
    /// Typed over the cell (arrows leave it) or opened to be corrected (arrows move in it).
    property bool quickEdit: true
    /// «cell» or «bar»: where the cursor is.
    property string editPlace: "cell"
    readonly property bool inFormula: editing && editText.charAt(0) === "="
    readonly property var references: inFormula ? book.references(editText) : []
    readonly property var formulaHelp: inFormula ? book.formulaContext(editText, editCursor) : ({})
    readonly property var completions: inFormula && formulaHelp.token !== undefined && formulaHelp.token.length > 0 ? book.completions(formulaHelp.token) : []
    signal cursorWanted(int position)

    /// The chart in hand, or 0.
    property int chartId: 0
    readonly property var chart: {
        book.revision
        if (chartId === 0 || !book.open)
            return null
        for (const c of book.charts())
            if (c.id === chartId)
                return c
        return null
    }

    property bool showSidebar: App.setting("sidebar", true)
    property bool showInspector: App.setting("inspector", false)
    property bool concentrate: false
    property string notice: ""
    readonly property bool busy: palette.visible || prompt.visible || confirmDialog.visible || condDialog.visible || versions.visible || closeDialog.visible || filterPopup.visible

    width: App.testing ? App.testSize.width : App.setting("width", 1180)
    height: App.testing ? App.testSize.height : App.setting("height", 760)
    minimumWidth: 880
    minimumHeight: 520
    visible: true
    title: book.open ? book.title + " — Hojas" : "Hojas"
    color: App.glass ? "transparent" : Theme.window

    property bool closeAnyway: false
    onClosing: close => {
        if (App.testing)
            return
        if (editing)
            commit(0, 0)
        if (book.open && book.modified && !closeAnyway) {
            close.accepted = false
            closeDialog.open()
            return
        }
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("sidebar", showSidebar)
        App.setSetting("inspector", showInspector)
    }
    Component.onCompleted: {
        if (App.startProblem !== "")
            say(App.startProblem)
        if (App.testing)
            testStep.start()
        else
            sheet.takeFocus()
    }
    Connections {
        target: win.book
        function onDocumentChanged() {
            win.cancelEdit()
            win.chartId = 0
            win.select(0, 0, false)
            sheet.scrollTo(0, 0)
            sheet.takeFocus()
        }
        function onChanged() {
            // A sheet that shrank, or another sheet: stay inside it.
            if (win.curRow >= win.book.rows || win.curCol >= win.book.cols)
                win.select(Math.min(win.curRow, win.book.rows - 1), Math.min(win.curCol, win.book.cols - 1), false)
        }
    }

    function say(text) {
        notice = text
        if (text !== "")
            noticeTimer.restart()
    }
    function done(error) {
        if (error !== undefined && error !== "")
            say(error)
        return error === undefined || error === ""
    }

    // --- The selection -------------------------------------------------------------

    function select(row, col, extend) {
        row = Math.max(0, Math.min(row, 1048575))
        col = Math.max(0, Math.min(col, 16383))
        if (book.open)
            book.reach(row, col)
        curRow = row
        curCol = col
        if (!extend) {
            ancRow = row
            ancCol = col
        }
        chartId = 0
        sheet.reveal(row, col)
    }
    function selectArea(a) {
        if (a === undefined || a.r1 === undefined)
            return
        ancRow = a.r2
        ancCol = a.c2
        curRow = a.r1
        curCol = a.c1
        chartId = 0
        sheet.reveal(a.r1, a.c1)
    }
    function move(dr, dc, extend, far) {
        if (!book.open)
            return
        let row = curRow
        let col = curCol
        if (far) {
            const to = book.jump(row, col, dr, dc)
            row = to.row
            col = to.col
        } else {
            // Out of a merge by its far side.
            const e = book.extent(row, col)
            if (!extend) {
                if (dr > 0) row = e.r2
                if (dc > 0) col = e.c2
                if (dr < 0) row = e.r1
                if (dc < 0) col = e.c1
            }
            if (dr !== 0)
                row = book.nextRow(row, dr)
            if (dc !== 0)
                col = book.nextCol(col, dc)
        }
        select(row, col, extend)
    }
    function selectionName() {
        const s = selection
        return single ? book.name(curRow, curCol, curRow, curCol) : book.name(s.r1, s.c1, s.r2, s.c2)
    }

    // --- Editing ---------------------------------------------------------------------

    function beginEdit(text, place, quick) {
        if (!book.open)
            return
        chartId = 0
        const e = book.extent(curRow, curCol)
        if (e.r1 !== curRow || e.c1 !== curCol) {
            curRow = e.r1
            curCol = e.c1
        }
        editText = text === undefined ? book.input(curRow, curCol) : text
        editPlace = place === undefined ? "cell" : place
        quickEdit = quick === undefined ? text !== undefined : quick
        editCursor = editText.length
        editing = true
        sheet.reveal(curRow, curCol)
        if (editPlace === "bar")
            formulaBar.takeFocus()
        else
            sheet.focusEditor()
        cursorWanted(editText.length)
    }
    function commit(dr, dc, everywhere) {
        if (!editing)
            return true
        const s = selection
        const error = everywhere && !single ? book.setInputs(s.r1, s.c1, s.r2, s.c2, editText) : book.setInput(curRow, curCol, editText)
        if (error !== "") {
            say(error)
            return false
        }
        editing = false
        editText = ""
        sheet.takeFocus()
        if ((dr !== 0 || dc !== 0) && !everywhere)
            move(dr, dc, false, false)
        return true
    }
    function cancelEdit() {
        if (!editing)
            return
        editing = false
        editText = ""
        sheet.takeFocus()
    }
    /// Types a reference where the cursor is, when a formula is waiting for one.
    function wantsReference() {
        if (!inFormula)
            return false
        const before = editText.slice(0, editCursor).replace(/\s+$/, "")
        return before !== "" && "=+-*/^(;,&<>:".includes(before.charAt(before.length - 1))
    }
    function insertReference(name) {
        // Replace the reference just put in, when the pointer moves on.
        const start = pointStart >= 0 && pointStart <= editCursor ? pointStart : editCursor
        editText = editText.slice(0, start) + name + editText.slice(editCursor)
        pointStart = start
        editCursor = start + name.length
        cursorWanted(editCursor)
    }
    property int pointStart: -1
    onEditingChanged: pointStart = -1

    function acceptCompletion(index) {
        if (completions.length === 0)
            return false
        const item = completions[Math.max(0, Math.min(index, completions.length - 1))]
        const start = formulaHelp.tokenStart
        const tail = editText.slice(editCursor)
        const open = tail.charAt(0) === "(" ? "" : "("
        editText = editText.slice(0, start) + item.name + open + tail
        editCursor = start + item.name.length + 1
        cursorWanted(editCursor)
        return true
    }

    // --- What the actions do -----------------------------------------------------------

    function style(key, value) {
        const s = selection
        book.setStyle(s.r1, s.c1, s.r2, s.c2, key, value)
    }
    function toggleStyle(key) {
        style(key, !info[key])
    }
    function format(family) {
        const s = selection
        book.setFormat(s.r1, s.c1, s.r2, s.c2, family)
    }
    function copy(cut) {
        if (!book.open)
            return
        const s = selection
        App.setClipboard(book.copy(s.r1, s.c1, s.r2, s.c2, cut))
    }
    function paste(what) {
        if (!book.open)
            return
        const s = selection
        selectArea(book.paste(s.r1, s.c1, s.r2, s.c2, App.clipboard(), what))
    }
    function aggregate(name) {
        const s = selection
        if (single) {
            // On one cell: start the formula and let the range be chosen.
            beginEdit("=" + name + "(", "cell", true)
            return
        }
        const a = book.aggregate(s.r1, s.c1, s.r2, s.c2, name)
        if (a.r1 !== undefined)
            sheet.reveal(a.r2, a.c2)
    }
    function region() {
        // What an action on «the data» means: the selection, or the block around the cell.
        return single ? book.region(curRow, curCol) : selection
    }
    function addChart(type) {
        const a = region()
        const id = book.addChart(a.r1, a.c1, a.r2, a.c2, type)
        if (id > 0) {
            chartId = id
            showInspector = true
        } else {
            say("Selecciona los datos del gráfico: al menos una columna de números.")
        }
    }
    function sortBy(ascending) {
        const a = region()
        if (a.r2 <= a.r1)
            return say("No hay filas que ordenar.")
        book.sort(a.r1, a.c1, a.r2, a.c2, Math.max(a.c1, Math.min(curCol, a.c2)), ascending)
    }
    function toggleFilter() {
        const a = region()
        book.toggleFilter(a.r1, a.c1, a.r2, a.c2)
    }
    function makeTable() {
        if (table.index !== undefined)
            return book.removeTable(table.index)
        const a = region()
        if (!book.makeTable(a.r1, a.c1, a.r2, a.c2))
            say("Una tabla necesita una fila de títulos y al menos una de datos, y no puede pisar otra tabla.")
    }
    function toggleMerge() {
        const s = selection
        if (info.merged)
            book.unmerge(s.r1, s.c1, s.r2, s.c2)
        else if (!single && !book.merge(s.r1, s.c1, s.r2, s.c2))
            say("No se pueden combinar celdas que ya forman parte de otra combinación o de una tabla.")
    }
    function freeze() {
        if (book.freezeRows > 0 || book.freezeCols > 0)
            book.setFreeze(0, 0)
        else
            book.setFreeze(curRow, curCol)
    }
    function insert(what) {
        const s = selection
        if (what === "row") book.insertRows(s.r1, s.r2 - s.r1 + 1)
        if (what === "col") book.insertCols(s.c1, s.c2 - s.c1 + 1)
        if (what === "-row") book.deleteRows(s.r1, s.r2 - s.r1 + 1)
        if (what === "-col") book.deleteCols(s.c1, s.c2 - s.c1 + 1)
    }
    function zoomBy(step) {
        const steps = [0.5, 0.75, 0.9, 1, 1.1, 1.25, 1.5, 2, 3]
        let i = 0
        for (let k = 0; k < steps.length; ++k)
            if (Math.abs(steps[k] - book.zoom) < Math.abs(steps[i] - book.zoom))
                i = k
        book.zoom = step === 0 ? 1 : steps[Math.max(0, Math.min(steps.length - 1, i + step))]
    }
    function save() {
        if (!book.open)
            return
        if (editing && !commit(0, 0))
            return
        if (book.path === "")
            return saveDialog.ask("save")
        if (done(App.save()))
            say("Guardado")
    }
    function openFile(path) {
        if (done(App.open(path)) && !book.open)
            say("")
    }
    function renameDocument() {
        prompt.ask("Nombre del documento", book.title, "Renombrar", text => done(book.rename(text)))
    }
    function renameSheet(index) {
        prompt.ask("Nombre de la hoja", book.sheets[index], "Renombrar", text => {
            if (!book.renameSheet(index, text))
                say("Ese nombre no vale: está en uso, vacío o lleva \\ / ? * [ ] :")
        })
    }
    function removeSheet(index) {
        if (book.sheets.length < 2)
            return say("Un documento tiene al menos una hoja.")
        confirmDialog.ask("¿Eliminar «" + book.sheets[index] + "»?", "La hoja y todo lo que contiene desaparecen del documento. Se puede deshacer.", "Eliminar", () => book.removeSheet(index))
    }
    function closeDocument() {
        if (editing)
            commit(0, 0)
        if (book.modified)
            closeDialog.openFor("close")
        else
            book.close()
    }
    function goTo(text) {
        const a = book.parseName(text.trim())
        if (!a.ok)
            return say("«" + text + "» no es una celda ni un rango.")
        selectArea(a)
        sheet.takeFocus()
    }

    // --- Every action there is: the palette, the shortcuts and --do come from here ------

    readonly property var actions: [
        {id: "new", title: "Nueva hoja de cálculo", section: "Archivo", shortcut: "Ctrl+N", symbol: "file-plus", always: true, run: a => book.open && !App.testing ? App.launch(["--new", a || "blank"]) : book.create(a || "blank")},
        {id: "open", title: "Abrir…", section: "Archivo", shortcut: "Ctrl+O", symbol: "folder-open", always: true, run: a => a ? openFile(a) : openDialog.open()},
        {id: "save", title: "Guardar", section: "Archivo", shortcut: "Ctrl+S", symbol: "save", run: a => a ? done(App.saveAs(a)) : save()},
        {id: "save-as", title: "Guardar como…", section: "Archivo", shortcut: "Ctrl+Shift+S", symbol: "save", run: a => a ? done(App.saveAs(a)) : saveDialog.ask("save")},
        {id: "export-pdf", title: "Exportar como PDF…", section: "Archivo", shortcut: "", symbol: "share", run: a => a ? done(book.exportTo(a)) : saveDialog.ask("pdf")},
        {id: "export-csv", title: "Exportar la hoja como CSV…", section: "Archivo", shortcut: "", symbol: "share", run: a => a ? done(book.exportTo(a)) : saveDialog.ask("csv")},
        {id: "export-xlsx", title: "Exportar una copia en Excel (.xlsx)…", section: "Archivo", shortcut: "", symbol: "share", run: a => a ? done(book.exportTo(a)) : saveDialog.ask("xlsx")},
        {id: "versions", title: "Versiones del documento…", section: "Archivo", shortcut: "", symbol: "history", run: () => versions.show()},
        {id: "rename", title: "Renombrar el documento…", section: "Archivo", shortcut: "", symbol: "pencil", run: () => renameDocument()},
        {id: "close", title: "Cerrar el documento", section: "Archivo", shortcut: "Ctrl+W", symbol: "x", run: () => closeDocument()},

        {id: "undo", title: "Deshacer", section: "Edición", shortcut: "Ctrl+Z", symbol: "undo-2", run: () => book.undo()},
        {id: "redo", title: "Rehacer", section: "Edición", shortcut: "Ctrl+Shift+Z", symbol: "redo-2", run: () => book.redo()},
        {id: "cut", title: "Cortar", section: "Edición", shortcut: "Ctrl+X", symbol: "scissors", run: () => copy(true)},
        {id: "copy", title: "Copiar", section: "Edición", shortcut: "Ctrl+C", symbol: "copy", run: () => copy(false)},
        {id: "paste", title: "Pegar", section: "Edición", shortcut: "Ctrl+V", symbol: "clipboard", run: () => paste("all")},
        {id: "paste-values", title: "Pegar solo los valores", section: "Edición", shortcut: "Ctrl+Shift+V", symbol: "clipboard", run: () => paste("values")},
        {id: "paste-formats", title: "Pegar solo el formato", section: "Edición", shortcut: "", symbol: "clipboard", run: () => paste("formats")},
        {id: "clear", title: "Borrar el contenido", section: "Edición", hint: "Supr", symbol: "trash-2", run: () => book.clear(selection.r1, selection.c1, selection.r2, selection.c2, true, false)},
        {id: "clear-format", title: "Quitar el formato", section: "Edición", shortcut: "Ctrl+M", symbol: "trash-2", run: () => book.clear(selection.r1, selection.c1, selection.r2, selection.c2, false, true)},
        {id: "fill-down", title: "Rellenar hacia abajo", section: "Edición", shortcut: "Ctrl+D", symbol: "arrow-down", run: () => book.fillDown(selection.r1, selection.c1, selection.r2, selection.c2)},
        {id: "fill-right", title: "Rellenar hacia la derecha", section: "Edición", shortcut: "Ctrl+R", symbol: "arrow-right", run: () => book.fillRight(selection.r1, selection.c1, selection.r2, selection.c2)},
        {id: "select-all", title: "Seleccionar todo", section: "Edición", shortcut: "Ctrl+A", symbol: "grid-2x2", run: () => { const u = book.usedArea(); selectArea({r1: 0, c1: 0, r2: Math.max(u.r2, 0), c2: Math.max(u.c2, 0)}) }},
        {id: "find", title: "Buscar y reemplazar", section: "Edición", shortcut: "Ctrl+F", symbol: "search", run: a => findBar.show(a)},
        {id: "goto", title: "Ir a la celda…", section: "Edición", shortcut: "Ctrl+G", symbol: "hash", run: a => a ? goTo(a) : formulaBar.focusName()},

        {id: "insert-row", title: "Insertar filas encima", section: "Filas y columnas", shortcut: "", symbol: "rows-3", run: () => insert("row")},
        {id: "insert-col", title: "Insertar columnas a la izquierda", section: "Filas y columnas", shortcut: "", symbol: "columns-3", run: () => insert("col")},
        {id: "delete-row", title: "Eliminar las filas", section: "Filas y columnas", shortcut: "", symbol: "rows-3", run: () => insert("-row")},
        {id: "delete-col", title: "Eliminar las columnas", section: "Filas y columnas", shortcut: "", symbol: "columns-3", run: () => insert("-col")},
        {id: "fit-col", title: "Ajustar el ancho al contenido", section: "Filas y columnas", shortcut: "", symbol: "chevrons-left-right", run: () => book.fitCols(selection.c1, selection.c2)},
        {id: "hide-row", title: "Ocultar las filas", section: "Filas y columnas", shortcut: "", symbol: "eye-off", run: () => book.setRowsHidden(selection.r1, selection.r2, true)},
        {id: "hide-col", title: "Ocultar las columnas", section: "Filas y columnas", shortcut: "", symbol: "eye-off", run: () => book.setColsHidden(selection.c1, selection.c2, true)},
        {id: "show-all", title: "Mostrar las filas y columnas ocultas", section: "Filas y columnas", shortcut: "", symbol: "eye", run: () => { book.beginGroup(); book.setRowsHidden(0, book.rows - 1, false); book.setColsHidden(0, book.cols - 1, false); book.endGroup() }},
        {id: "freeze", title: "Inmovilizar hasta esta celda (o soltar)", section: "Filas y columnas", shortcut: "", symbol: "pin", run: () => freeze()},
        {id: "merge", title: "Combinar o separar celdas", section: "Filas y columnas", shortcut: "", symbol: "table-cells-merge", run: () => toggleMerge()},
        {id: "type-text", title: "Tipo de columna: texto", section: "Filas y columnas", shortcut: "", symbol: "type", run: () => book.setColumnType(curCol, "text")},
        {id: "type-number", title: "Tipo de columna: número", section: "Filas y columnas", shortcut: "", symbol: "hash", run: () => book.setColumnType(curCol, "number")},
        {id: "type-date", title: "Tipo de columna: fecha", section: "Filas y columnas", shortcut: "", symbol: "calendar", run: () => book.setColumnType(curCol, "date")},
        {id: "type-check", title: "Tipo de columna: casilla", section: "Filas y columnas", shortcut: "", symbol: "square-check", run: () => book.setColumnType(curCol, "check")},

        {id: "bold", title: "Negrita", section: "Formato", shortcut: "Ctrl+B", symbol: "bold", run: () => toggleStyle("bold")},
        {id: "italic", title: "Cursiva", section: "Formato", shortcut: "Ctrl+I", symbol: "italic", run: () => toggleStyle("italic")},
        {id: "underline", title: "Subrayado", section: "Formato", shortcut: "Ctrl+U", symbol: "underline", run: () => toggleStyle("underline")},
        {id: "strike", title: "Tachado", section: "Formato", shortcut: "", symbol: "strikethrough", run: () => toggleStyle("strike")},
        {id: "align-left", title: "Alinear a la izquierda", section: "Formato", shortcut: "Ctrl+L", symbol: "align-left", run: () => style("align", info.align === 1 ? 0 : 1)},
        {id: "align-center", title: "Centrar", section: "Formato", shortcut: "Ctrl+E", symbol: "align-center", run: () => style("align", info.align === 2 ? 0 : 2)},
        {id: "align-right", title: "Alinear a la derecha", section: "Formato", shortcut: "", symbol: "align-right", run: () => style("align", info.align === 3 ? 0 : 3)},
        {id: "wrap", title: "Ajustar el texto a la celda", section: "Formato", shortcut: "", symbol: "wrap-text", run: () => toggleStyle("wrap")},
        {id: "format-general", title: "Formato general", section: "Formato", shortcut: "", symbol: "hash", run: () => format("general")},
        {id: "format-number", title: "Formato de número", section: "Formato", shortcut: "Ctrl+Shift+1", symbol: "hash", run: () => format("number")},
        {id: "format-currency", title: "Formato de moneda (€)", section: "Formato", shortcut: "Ctrl+Shift+4", symbol: "euro", run: () => format("currency")},
        {id: "format-percent", title: "Formato de porcentaje", section: "Formato", shortcut: "Ctrl+Shift+5", symbol: "percent", run: () => format("percent")},
        {id: "format-date", title: "Formato de fecha", section: "Formato", shortcut: "Ctrl+Shift+3", symbol: "calendar", run: () => format("date")},
        {id: "format-time", title: "Formato de hora", section: "Formato", shortcut: "", symbol: "clock", run: () => format("time")},
        {id: "format-text", title: "Formato de texto", section: "Formato", shortcut: "", symbol: "type", run: () => format("text")},
        {id: "more-decimals", title: "Más decimales", section: "Formato", shortcut: "", symbol: "plus", run: () => book.changeDecimals(selection.r1, selection.c1, selection.r2, selection.c2, 1)},
        {id: "fewer-decimals", title: "Menos decimales", section: "Formato", shortcut: "", symbol: "minus", run: () => book.changeDecimals(selection.r1, selection.c1, selection.r2, selection.c2, -1)},
        {id: "borders-all", title: "Bordes en todas las celdas", section: "Formato", shortcut: "", symbol: "grid-2x2", run: () => book.setBorders(selection.r1, selection.c1, selection.r2, selection.c2, "all")},
        {id: "borders-outline", title: "Borde alrededor", section: "Formato", shortcut: "", symbol: "square", run: () => book.setBorders(selection.r1, selection.c1, selection.r2, selection.c2, "outline")},
        {id: "borders-none", title: "Quitar los bordes", section: "Formato", shortcut: "", symbol: "square", run: () => book.setBorders(selection.r1, selection.c1, selection.r2, selection.c2, "none")},
        {id: "conditional", title: "Formato condicional…", section: "Formato", shortcut: "", symbol: "palette", run: () => condDialog.show()},
        {id: "conditional-clear", title: "Quitar el formato condicional de la selección", section: "Formato", shortcut: "", symbol: "palette", run: () => book.clearConditionals(selection.r1, selection.c1, selection.r2, selection.c2)},

        {id: "sum", title: "Suma", section: "Análisis rápido", shortcut: "Alt+=", symbol: "sigma", run: () => aggregate("SUMA")},
        {id: "average", title: "Media", section: "Análisis rápido", shortcut: "", symbol: "sigma", run: () => aggregate("PROMEDIO")},
        {id: "count", title: "Recuento", section: "Análisis rápido", shortcut: "", symbol: "sigma", run: () => aggregate("CONTARA")},
        {id: "max", title: "Máximo", section: "Análisis rápido", shortcut: "", symbol: "sigma", run: () => aggregate("MAX")},
        {id: "min", title: "Mínimo", section: "Análisis rápido", shortcut: "", symbol: "sigma", run: () => aggregate("MIN")},
        {id: "sort-asc", title: "Ordenar de menor a mayor (A → Z)", section: "Análisis rápido", shortcut: "", symbol: "arrow-down-a-z", run: () => sortBy(true)},
        {id: "sort-desc", title: "Ordenar de mayor a menor (Z → A)", section: "Análisis rápido", shortcut: "", symbol: "arrow-up-a-z", run: () => sortBy(false)},
        {id: "filter", title: "Poner o quitar el filtro", section: "Análisis rápido", shortcut: "Ctrl+Shift+L", symbol: "list-filter", run: () => toggleFilter()},
        {id: "table", title: "Convertir en tabla (o dejar de serlo)", section: "Análisis rápido", shortcut: "Ctrl+T", symbol: "table", run: () => makeTable()},
        {id: "table-totals", title: "Fila de totales de la tabla", section: "Análisis rápido", shortcut: "", symbol: "table", run: () => table.index !== undefined ? book.setTableTotals(table.index, !table.totals) : say("La celda no está en una tabla.")},

        {id: "chart-column", title: "Gráfico de columnas", section: "Gráficos", shortcut: "", symbol: "chart-column", run: () => addChart("column")},
        {id: "chart-bar", title: "Gráfico de barras", section: "Gráficos", shortcut: "", symbol: "chart-bar", run: () => addChart("bar")},
        {id: "chart-line", title: "Gráfico de líneas", section: "Gráficos", shortcut: "", symbol: "chart-line", run: () => addChart("line")},
        {id: "chart-area", title: "Gráfico de área", section: "Gráficos", shortcut: "", symbol: "chart-area", run: () => addChart("area")},
        {id: "chart-pie", title: "Gráfico circular", section: "Gráficos", shortcut: "", symbol: "chart-pie", run: () => addChart("pie")},
        {id: "chart-scatter", title: "Gráfico de dispersión", section: "Gráficos", shortcut: "", symbol: "chart-scatter", run: () => addChart("scatter")},
        {id: "chart-remove", title: "Eliminar el gráfico seleccionado", section: "Gráficos", shortcut: "", symbol: "trash-2", run: () => { if (chartId) { book.removeChart(chartId); chartId = 0 } }},

        {id: "sheet-add", title: "Añadir una hoja", section: "Hojas", shortcut: "Shift+F11", symbol: "plus", run: () => book.addSheet()},
        {id: "sheet-rename", title: "Renombrar la hoja…", section: "Hojas", shortcut: "", symbol: "pencil", run: () => renameSheet(book.sheet)},
        {id: "sheet-duplicate", title: "Duplicar la hoja", section: "Hojas", shortcut: "", symbol: "copy", run: () => book.duplicateSheet(book.sheet)},
        {id: "sheet-remove", title: "Eliminar la hoja…", section: "Hojas", shortcut: "", symbol: "trash-2", run: () => removeSheet(book.sheet)},
        {id: "sheet-next", title: "Hoja siguiente", section: "Hojas", shortcut: "Ctrl+PgDown", symbol: "chevron-right", run: () => book.sheet = Math.min(book.sheets.length - 1, book.sheet + 1)},
        {id: "sheet-previous", title: "Hoja anterior", section: "Hojas", shortcut: "Ctrl+PgUp", symbol: "chevron-left", run: () => book.sheet = Math.max(0, book.sheet - 1)},

        {id: "palette", title: "Órdenes", section: "Ver", shortcut: "Ctrl+K", symbol: "search", always: true, run: a => palette.show(a)},
        {id: "sidebar", title: "Barra lateral", section: "Ver", shortcut: "Ctrl+Alt+S", symbol: "panel-left", run: () => showSidebar = !showSidebar},
        {id: "inspector", title: "Inspector de formato", section: "Ver", shortcut: "Ctrl+Alt+I", symbol: "panel-right", run: () => showInspector = !showInspector},
        {id: "focus", title: "Modo concentración", section: "Ver", shortcut: "Ctrl+Shift+F", symbol: "maximize-2", run: () => concentrate = !concentrate},
        {id: "zoom-in", title: "Acercar", section: "Ver", shortcut: "Ctrl++", symbol: "zoom-in", run: () => zoomBy(1)},
        {id: "zoom-out", title: "Alejar", section: "Ver", shortcut: "Ctrl+-", symbol: "zoom-out", run: () => zoomBy(-1)},
        {id: "zoom-reset", title: "Tamaño real", section: "Ver", shortcut: "Ctrl+0", symbol: "search", run: () => zoomBy(0)},
    ]

    function action(id) {
        for (const a of actions)
            if (a.id === id)
                return a
        return null
    }
    /// Runs an action by its id: what a button, a shortcut, the palette and --do all call.
    function run(id, argument) {
        const a = action(id)
        if (a === null)
            return false
        if (!book.open && !a.always)
            return true
        // What was being typed goes into its cell first.
        if (editing && !["palette", "copy", "cut", "paste", "undo", "redo", "select-all"].includes(id) && !commit(0, 0))
            return true
        a.run(argument)
        return true
    }

    Instantiator {
        model: win.actions.filter(a => a.shortcut !== undefined && a.shortcut !== "")
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.id === "redo" ? ["Ctrl+Shift+Z", "Ctrl+Y"] : (modelData.id === "zoom-in" ? ["Ctrl++", "Ctrl+="] : [modelData.shortcut])
            enabled: !win.busy || modelData.id === "palette"
            onActivated: win.run(modelData.id)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: win.concentrate && !win.editing && !win.busy
        onActivated: win.concentrate = false
    }

    // --- The window ----------------------------------------------------------------------

    readonly property bool chromeShown: book.open && !concentrate

    Rectangle {
        id: bar
        visible: win.chromeShown
        width: parent.width
        height: visible ? 46 : 0
        color: Theme.chrome
        TopBar {
            anchors.fill: parent
            host: win
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    Sidebar {
        id: sidebar
        host: win
        visible: win.chromeShown && win.showSidebar
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        width: visible ? 208 : 0
    }

    Inspector {
        id: inspector
        host: win
        visible: win.chromeShown && win.showInspector
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: visible ? 264 : 0
    }

    Rectangle {
        id: body
        visible: book.open
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: inspector.left
        color: Theme.window

        FormulaBar {
            id: formulaBar
            host: win
            visible: win.chromeShown
            width: parent.width
            height: visible ? implicitHeight : 0
        }

        SheetView {
            id: sheet
            host: win
            anchors.top: formulaBar.bottom
            anchors.bottom: tabs.top
            width: parent.width
        }

        FindBar {
            id: findBar
            host: win
            anchors.top: formulaBar.bottom
            anchors.right: parent.right
            anchors.margins: 10
        }

        // The sheets, as tabs.
        Rectangle {
            id: tabs
            visible: win.chromeShown
            anchors.bottom: status.top
            width: parent.width
            height: visible ? 32 : 0
            color: Theme.chrome
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            TButton {
                id: addTab
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                symbol: "plus"
                size: 15
                onClicked: win.run("sheet-add")
            }
            ListView {
                id: tabList
                anchors.left: addTab.right
                anchors.leftMargin: 6
                anchors.right: parent.right
                anchors.rightMargin: 8
                height: parent.height
                orientation: ListView.Horizontal
                clip: true
                spacing: 2
                model: win.book.sheets
                currentIndex: win.book.sheet
                delegate: Item {
                    id: tab
                    required property int index
                    required property string modelData
                    readonly property bool on: win.book.sheet === index
                    width: Math.min(180, tabLabel.implicitWidth + 26)
                    height: tabList.height
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        radius: 6
                        color: tab.on ? Theme.sideSelection : (tabArea.containsMouse ? Theme.hover : "transparent")
                    }
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, 154)
                        text: tab.modelData
                        elide: Text.ElideRight
                        color: tab.on ? Theme.text : Theme.dim
                        font.pixelSize: 12
                        font.weight: tab.on ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: tabArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (win.editing && !win.commit(0, 0))
                                return
                            win.book.sheet = tab.index
                            if (mouse.button === Qt.RightButton)
                                sheetMenu.popup()
                        }
                        onDoubleClicked: win.renameSheet(tab.index)
                    }
                }
            }
        }

        // What the selection adds up to, and the zoom.
        Rectangle {
            id: status
            visible: win.chromeShown
            anchors.bottom: parent.bottom
            width: parent.width
            height: visible ? 26 : 0
            color: Theme.chrome
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Text {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, (summary.visible ? summary.x : zoomRow.x) - 24)
                elide: Text.ElideRight
                color: Theme.dim
                font.pixelSize: 11
                text: {
                    if (win.notice !== "")
                        return win.notice
                    if (win.editing)
                        return win.inFormula ? "Fórmula · Intro para aceptar, Esc para cancelar" : "Editando"
                    const s = win.selection
                    const cells = (s.r2 - s.r1 + 1) * (s.c2 - s.c1 + 1)
                    let text = win.single ? win.selectionName() : win.selectionName() + " · " + (s.r2 - s.r1 + 1) + " × " + (s.c2 - s.c1 + 1) + " celdas"
                    if (win.book.freezeRows > 0 || win.book.freezeCols > 0)
                        text += " · Inmovilizado"
                    if (win.book.revision >= 0 && win.book.hasFilter())
                        text += " · Filtro"
                    return text
                }
            }
            Row {
                id: summary
                anchors.right: zoomRow.left
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                visible: !win.single && win.stats.count > 0 && parent.width > 520
                Repeater {
                    model: win.stats.numbers > 0
                        ? [["Suma", win.stats.sumText], ["Media", win.stats.averageText], ["Mín.", win.stats.minText], ["Máx.", win.stats.maxText], ["Recuento", String(win.stats.count)]]
                        : [["Recuento", String(win.stats.count === undefined ? 0 : win.stats.count)]]
                    Row {
                        required property var modelData
                        required property int index
                        spacing: 4
                        // The two least asked for give way in a narrow window.
                        visible: status.width > 1040 || (index !== 2 && index !== 3)
                        Text { text: parent.modelData[0]; color: Theme.dim; font.pixelSize: 11 }
                        Text { text: parent.modelData[1]; color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                    }
                }
            }
            Row {
                id: zoomRow
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                TButton { width: 24; height: 20; size: 12; symbol: "minus"; onClicked: win.run("zoom-out") }
                Text {
                    width: 40
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: Math.round(win.book.zoom * 100) + " %"
                    color: Theme.dim
                    font.pixelSize: 11
                    MouseArea { anchors.fill: parent; onClicked: win.run("zoom-reset") }
                }
                TButton { width: 24; height: 20; size: 12; symbol: "plus"; onClicked: win.run("zoom-in") }
            }
        }

        // In concentration: how to get out, quietly.
        Rectangle {
            visible: win.concentrate && win.book.open
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            width: leave.implicitWidth + 22
            height: 26
            radius: 13
            color: Theme.menu
            border.color: Theme.border
            opacity: leaveArea.containsMouse ? 1 : 0.75
            Text {
                id: leave
                anchors.centerIn: parent
                text: "Esc para salir de concentración"
                color: Theme.dim
                font.pixelSize: 11
            }
            MouseArea { id: leaveArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.concentrate = false }
        }
    }

    Welcome {
        host: win
        visible: !win.book.open
        anchors.fill: parent
    }

    // --- Menus, dialogs ---------------------------------------------------------------------

    FMenu {
        id: sheetMenu
        FMenuItem { text: "Renombrar…"; onTriggered: win.run("sheet-rename") }
        FMenuItem { text: "Duplicar"; onTriggered: win.run("sheet-duplicate") }
        FMenuItem { text: "Mover a la izquierda"; enabled: win.book.sheet > 0; onTriggered: win.book.moveSheet(win.book.sheet, win.book.sheet - 1) }
        FMenuItem { text: "Mover a la derecha"; enabled: win.book.sheet < win.book.sheets.length - 1; onTriggered: win.book.moveSheet(win.book.sheet, win.book.sheet + 1) }
        FSeparator {}
        FMenuItem { text: "Eliminar…"; enabled: win.book.sheets.length > 1; onTriggered: win.run("sheet-remove") }
    }
    function sheetMenuAt() { sheetMenu.popup() }

    readonly property bool modalOpen: palette.opened || prompt.opened || condDialog.opened || versions.opened
    CommandPalette {
        id: palette
        host: win
    }
    PromptDialog {
        id: prompt
    }
    ConfirmDialog {
        id: confirmDialog
    }
    CondDialog {
        id: condDialog
        host: win
    }
    VersionsPanel {
        id: versions
        host: win
    }
    FilterPopup {
        id: filterPopup
        host: win
    }
    function openFilter(col, x, y) {
        filterPopup.show(col, x, y)
    }

    Popup {
        id: closeDialog
        property string then: "quit"
        function openFor(what) {
            then = what
            open()
        }
        function finish() {
            close()
            if (then === "close") {
                win.book.close()
            } else {
                win.closeAnyway = true
                win.close()
            }
        }
        onClosed: then = "quit"
        modal: true
        anchors.centerIn: parent
        width: 400
        padding: 18
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { radius: 12; color: Theme.menu; border.color: Theme.border }
        Overlay.modal: Rectangle { color: "#40000000" }
        contentItem: Column {
            spacing: 10
            Text {
                width: parent.width
                text: "¿Guardar los cambios de «" + win.book.title + "»?"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: "Si no los guardas, lo que has cambiado desde la última vez se pierde."
                color: Theme.dim
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Item { width: 1; height: 2 }
            Item {
                width: parent.width
                height: 28
                DialogButton {
                    text: "No guardar"
                    danger: true
                    onClicked: closeDialog.finish()
                }
                Row {
                    anchors.right: parent.right
                    spacing: 8
                    DialogButton { text: "Cancelar"; onClicked: closeDialog.close() }
                    DialogButton {
                        text: "Guardar"
                        primary: true
                        onClicked: {
                            if (win.book.path === "") {
                                const what = closeDialog.then
                                closeDialog.close()
                                saveDialog.ask(what === "close" ? "save-close" : "save-quit")
                            } else if (win.done(App.save())) {
                                closeDialog.finish()
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: openDialog
        title: "Abrir"
        fileMode: FileDialog.OpenFile
        currentFolder: App.folder()
        nameFilters: ["Hojas de cálculo (*.xlsx *.xlsm *.ods *.csv *.tsv *.tab *.txt)", "Todos los archivos (*)"]
        onAccepted: win.openFile(App.localPath(selectedFile))
    }
    FileDialog {
        id: saveDialog
        /// save, pdf, csv or xlsx; save-close and save-quit go on to leave.
        property string purpose: "save"
        function ask(what) {
            purpose = what
            const suffix = what === "pdf" ? "pdf" : (what === "csv" ? "csv" : (what.startsWith("save") && ["csv", "tsv"].includes(win.book.format()) ? win.book.format() : "xlsx"))
            nameFilters = what === "pdf" ? ["PDF (*.pdf)"] : (what === "csv" ? ["Valores separados por comas (*.csv)", "Valores separados por tabuladores (*.tsv)"]
                : ["Libro de Excel (*.xlsx)", "Valores separados por comas (*.csv)", "Valores separados por tabuladores (*.tsv)"])
            defaultSuffix = suffix
            currentFolder = App.folder()
            selectedFile = App.fileUrl(App.suggestedPath(suffix))
            open()
        }
        title: purpose.startsWith("save") ? "Guardar" : "Exportar"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const path = App.localPath(selectedFile)
            if (!purpose.startsWith("save")) {
                if (win.done(win.book.exportTo(path)))
                    win.say("Exportado en " + path)
                return
            }
            if (!win.done(App.saveAs(path)))
                return
            win.say("Guardado")
            if (purpose === "save-close") {
                win.book.close()
            } else if (purpose === "save-quit") {
                win.closeAnyway = true
                win.close()
            }
        }
    }

    Timer {
        id: noticeTimer
        interval: 6000
        onTriggered: win.notice = ""
    }

    // --- Tests: what --do can ask for -----------------------------------------------------

    function runAction(text) {
        const colon = text.indexOf(":")
        const verb = colon < 0 ? text : text.slice(0, colon)
        // «¦» stands for the «;» that separates actions.
        const arg = colon < 0 ? "" : text.slice(colon + 1).replace(/¦/g, ";")
        switch (verb) {
        case "sample":
            book.sample()
            return
        case "select":
            goTo(arg)
            return
        case "type":
            // Types into the cell and accepts, moving down like Intro.
            beginEdit(arg, "cell", true)
            commit(1, 0)
            return
        case "edit":
            // Leaves the cell open, to look at the editor and its help.
            beginEdit(arg === "" ? undefined : arg, "cell", arg !== "")
            return
        case "edit-bar":
            beginEdit(arg === "" ? undefined : arg, "bar", false)
            return
        case "accept":
            commit(0, 0)
            return
        case "complete":
            acceptCompletion(0)
            return
        case "point":
            // What a click on a cell does while a formula waits for a reference.
            if (wantsReference())
                insertReference(arg)
            return
        case "sheet":
            book.sheet = parseInt(arg)
            return
        case "dark":
        case "light":
            return
        case "inspector-on":
            showInspector = true
            return
        case "sidebar-off":
            showSidebar = false
            return
        case "chart-select":
            chartId = book.charts().length > 0 ? book.charts()[Math.max(0, parseInt(arg || "0"))].id : 0
            return
        case "filter-menu":
            sheet.openFilterAt(curRow, curCol)
            return
        case "filter-only":
            book.setFilterAllowed(curCol, arg.split("|"), false)
            return
        case "context-menu":
            sheet.openContextMenu()
            return
        case "share-menu":
            shareMenuWanted()
            return
        case "scroll":
            sheet.scrollTo(parseInt(arg.split(",")[0]), parseInt(arg.split(",")[1] || "0"))
            return
        case "zoom":
            book.zoom = parseFloat(arg)
            return
        case "width":
            book.setColWidth(selection.c1, selection.c2, parseFloat(arg))
            return
        case "height":
            book.setRowHeight(selection.r1, selection.r2, parseFloat(arg))
            return
        case "fill":
            style("fill", arg)
            return
        case "color":
            style("color", arg)
            return
        case "condition":
            // condition:>:100:#fde2e1:#b3261e
            const p = arg.split(":")
            book.addConditional(selection.r1, selection.c1, selection.r2, selection.c2, p[0], p[1] || "", "", p[2] || "#fde2e1", p[3] || "", false)
            return
        case "replace":
            findBar.show(arg.split("=")[0])
            findBar.replaceAllWith(arg.split("=")[1] || "")
            return
        case "recovery":
            book.saveRecovery()
            return
        case "say":
            say(arg)
            return
        }
        if (!run(verb, arg === "" ? undefined : arg))
            console.warn("unknown action", verb)
    }
    signal shareMenuWanted()

    Timer {
        id: testStep
        interval: 300
        onTriggered: {
            for (const action of App.testActions)
                win.runAction(action.trim())
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 900
        onTriggered: App.saveShot(win)
    }
}
