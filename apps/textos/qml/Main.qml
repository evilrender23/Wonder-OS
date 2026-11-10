import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import WonderlandTextos

ApplicationWindow {
    id: win

    readonly property var editor: App.editor
    readonly property var look: App.editor.format
    property bool sidebarShown: App.setting("sidebar", true)
    property bool inspectorShown: App.setting("inspector", true)
    /// Nothing but the sheet, the paragraph in hand lit and kept at mid height.
    property bool concentrating: false
    /// 0 fits the sheet to the room there is; anything else is the zoom asked for.
    property real zoomWish: App.setting("zoom", 0)
    readonly property real zoom: zoomWish > 0 ? zoomWish : Math.max(0.4, Math.min(1.25, (desk.width - 56) / editor.pageWidth))
    readonly property int sheetWidth: Math.round(editor.pageWidth * zoom)
    readonly property int sheetHeight: Math.round(editor.pageHeight * zoom)
    readonly property int sheetGap: 18
    /// Words the writer wants to reach; 0 for none.
    property int goal: App.setting("goal", 0)
    property bool blink: true
    property bool renaming: false
    /// What to do once the document in the window is safe (saved or given up).
    property var pending: null
    property bool leaving: false
    property string message

    width: App.testing ? App.testSize.width : App.setting("width", 1180)
    height: App.testing ? App.testSize.height : App.setting("height", 760)
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: App.started ? App.title + " — Textos" : "Textos"
    color: App.glass ? "transparent" : Theme.window

    onSidebarShownChanged: if (!App.testing) App.setSetting("sidebar", sidebarShown)
    onInspectorShownChanged: if (!App.testing) App.setSetting("inspector", inspectorShown)
    onZoomWishChanged: if (!App.testing) App.setSetting("zoom", zoomWish)
    onGoalChanged: if (!App.testing) App.setSetting("goal", goal)
    onClosing: close => {
        if (App.testing)
            return
        if (App.started && editor.modified && !leaving) {
            close.accepted = false
            guarded(() => {
                leaving = true
                win.close()
            })
            return
        }
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.closing()
    }

    // --- Everything Textos can do ------------------------------------------------
    // One register: the palette lists it, the shortcuts and the tool buttons run
    // it, and so does `--do`. `native` keys are the editor's own (they must not
    // be taken from a text field); `hidden` ones only make sense with an argument.
    readonly property var actions: [
        {id: "new", title: "Documento nuevo", section: "Archivo", symbol: "file-plus", shortcut: "Ctrl+N", run: () => App.started ? App.openWindow("") : App.start("blank")},
        {id: "newLetter", title: "Nuevo desde plantilla: Carta", section: "Archivo", symbol: "mail", run: () => fresh("letter")},
        {id: "newResume", title: "Nuevo desde plantilla: Currículum", section: "Archivo", symbol: "user", run: () => fresh("resume")},
        {id: "newReport", title: "Nuevo desde plantilla: Informe", section: "Archivo", symbol: "briefcase", run: () => fresh("report")},
        {id: "newMinutes", title: "Nuevo desde plantilla: Acta", section: "Archivo", symbol: "clock", run: () => fresh("minutes")},
        {id: "sample", title: "Documento de muestra", section: "Archivo", symbol: "file-text", run: () => fresh("sample")},
        {id: "open", title: "Abrir…", section: "Archivo", symbol: "folder-open", shortcut: "Ctrl+O", run: path => path ? App.open(path) : openDialog.open()},
        {id: "save", title: "Guardar", section: "Archivo", symbol: "save", shortcut: "Ctrl+S", needs: "document", run: () => save()},
        {id: "saveAs", title: "Guardar como…", section: "Archivo", symbol: "save", shortcut: "Ctrl+Shift+S", needs: "document", run: path => path ? App.saveAs(path) : askWhere()},
        {id: "exportPdf", title: "Exportar como PDF…", section: "Archivo", symbol: "file-down", shortcut: "Ctrl+Shift+E", needs: "document", run: path => exportAs("pdf", "PDF", path)},
        {id: "exportDocx", title: "Exportar como Word (.docx)…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("docx", "Documento de Word", path)},
        {id: "exportOdt", title: "Exportar como OpenDocument (.odt)…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("odt", "Texto OpenDocument", path)},
        {id: "exportRtf", title: "Exportar como RTF…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("rtf", "Texto enriquecido", path)},
        {id: "exportHtml", title: "Exportar como página web (.html)…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("html", "Página web", path)},
        {id: "exportMd", title: "Exportar como Markdown…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("md", "Markdown", path)},
        {id: "exportTxt", title: "Exportar como texto sin formato…", section: "Archivo", symbol: "file-down", needs: "document", run: path => exportAs("txt", "Texto", path)},
        {id: "versions", title: "Versiones…", section: "Archivo", symbol: "history", needs: "document", run: () => versions.open()},
        {id: "rename", title: "Cambiar el nombre…", section: "Archivo", symbol: "pilcrow", shortcut: "F2", needs: "document", run: name => name ? App.rename(name) : beginRename()},
        {id: "close", title: "Cerrar el documento", section: "Archivo", symbol: "x", shortcut: "Ctrl+W", needs: "document", run: () => guarded(() => App.closeDocument())},

        {id: "undo", title: "Deshacer", section: "Editar", symbol: "undo-2", shortcut: "Ctrl+Z", native: true, needs: "document", enabled: () => editor.canUndo, run: () => editor.undo()},
        {id: "redo", title: "Rehacer", section: "Editar", symbol: "redo-2", shortcut: "Ctrl+Shift+Z", native: true, needs: "document", enabled: () => editor.canRedo, run: () => editor.redo()},
        {id: "cut", title: "Cortar", section: "Editar", symbol: "copy", shortcut: "Ctrl+X", native: true, needs: "document", enabled: () => look.selection, run: () => editor.cut()},
        {id: "copy", title: "Copiar", section: "Editar", symbol: "copy", shortcut: "Ctrl+C", native: true, needs: "document", enabled: () => look.selection, run: () => editor.copy()},
        {id: "paste", title: "Pegar", section: "Editar", symbol: "copy", shortcut: "Ctrl+V", native: true, needs: "document", run: () => editor.paste()},
        {id: "selectAll", title: "Seleccionar todo", section: "Editar", symbol: "text-align-justify", shortcut: "Ctrl+A", native: true, needs: "document", run: () => editor.selectAll()},
        {id: "find", title: "Buscar…", section: "Editar", symbol: "search", shortcut: "Ctrl+F", needs: "document", run: text => findBar.show(false, text ?? editor.selectedText())},
        {id: "replace", title: "Buscar y reemplazar…", section: "Editar", symbol: "replace", shortcut: "Ctrl+H", needs: "document", run: text => findBar.show(true, text ?? editor.selectedText())},
        {id: "findNext", title: "Buscar el siguiente", section: "Editar", symbol: "chevron-down", shortcut: "F3", needs: "document", run: () => editor.findNext(true)},
        {id: "findPrevious", title: "Buscar el anterior", section: "Editar", symbol: "chevron-up", shortcut: "Shift+F3", needs: "document", run: () => editor.findNext(false)},

        {id: "bold", title: "Negrita", section: "Formato", symbol: "bold", shortcut: "Ctrl+B", native: true, needs: "document", run: () => editor.toggleBold()},
        {id: "italic", title: "Cursiva", section: "Formato", symbol: "italic", shortcut: "Ctrl+I", native: true, needs: "document", run: () => editor.toggleItalic()},
        {id: "underline", title: "Subrayado", section: "Formato", symbol: "underline", shortcut: "Ctrl+U", native: true, needs: "document", run: () => editor.toggleUnderline()},
        {id: "strike", title: "Tachado", section: "Formato", symbol: "strikethrough", shortcut: "Ctrl+Shift+X", needs: "document", run: () => editor.toggleStrike()},
        {id: "superscript", title: "Superíndice", section: "Formato", symbol: "superscript", shortcut: "Ctrl+.", needs: "document", run: () => editor.setVertical(look.vertical === 1 ? 0 : 1)},
        {id: "subscript", title: "Subíndice", section: "Formato", symbol: "subscript", shortcut: "Ctrl+,", needs: "document", run: () => editor.setVertical(look.vertical === 2 ? 0 : 2)},
        {id: "highlight", title: "Resaltar", section: "Formato", symbol: "highlighter", shortcut: "Ctrl+Shift+H", needs: "document", run: color => look.highlight !== "" && !color ? editor.clearHighlight() : editor.setHighlight(color ? "#" + color : "#ffe066")},
        {id: "bigger", title: "Letra más grande", section: "Formato", symbol: "type", shortcut: "Ctrl+]", needs: "document", run: () => editor.setPointSize(Math.min(200, look.size + 1))},
        {id: "smaller", title: "Letra más pequeña", section: "Formato", symbol: "type", shortcut: "Ctrl+[", needs: "document", run: () => editor.setPointSize(Math.max(4, look.size - 1))},
        {id: "color", title: "Color del texto", section: "Formato", symbol: "baseline", hidden: true, run: color => editor.setColor("#" + color)},
        {id: "family", title: "Fuente", section: "Formato", symbol: "type", hidden: true, run: family => editor.setFamily(family)},
        {id: "size", title: "Tamaño", section: "Formato", symbol: "type", hidden: true, run: size => editor.setPointSize(Number(size))},

        {id: "styleBody", title: "Estilo: Cuerpo", section: "Párrafo", symbol: "pilcrow", shortcut: "Ctrl+Alt+0", needs: "document", run: () => editor.applyStyle("body")},
        {id: "styleTitle", title: "Estilo: Título", section: "Párrafo", symbol: "type", shortcut: "Ctrl+Alt+T", needs: "document", run: () => editor.applyStyle("title")},
        {id: "styleH1", title: "Estilo: Encabezado 1", section: "Párrafo", symbol: "heading-1", shortcut: "Ctrl+Alt+1", needs: "document", run: () => editor.applyStyle("h1")},
        {id: "styleH2", title: "Estilo: Encabezado 2", section: "Párrafo", symbol: "heading-2", shortcut: "Ctrl+Alt+2", needs: "document", run: () => editor.applyStyle("h2")},
        {id: "styleH3", title: "Estilo: Encabezado 3", section: "Párrafo", symbol: "heading-3", shortcut: "Ctrl+Alt+3", needs: "document", run: () => editor.applyStyle("h3")},
        {id: "styleQuote", title: "Estilo: Cita", section: "Párrafo", symbol: "text-quote", shortcut: "Ctrl+Alt+Q", needs: "document", run: () => editor.applyStyle("quote")},
        {id: "styleCode", title: "Estilo: Código", section: "Párrafo", symbol: "code", shortcut: "Ctrl+Alt+C", needs: "document", run: () => editor.applyStyle("code")},
        {id: "alignLeft", title: "Alinear a la izquierda", section: "Párrafo", symbol: "text-align-start", shortcut: "Ctrl+L", needs: "document", run: () => editor.setAlignment("left")},
        {id: "alignCenter", title: "Centrar", section: "Párrafo", symbol: "text-align-center", shortcut: "Ctrl+E", needs: "document", run: () => editor.setAlignment("center")},
        {id: "alignRight", title: "Alinear a la derecha", section: "Párrafo", symbol: "text-align-end", shortcut: "Ctrl+R", needs: "document", run: () => editor.setAlignment("right")},
        {id: "alignJustify", title: "Justificar", section: "Párrafo", symbol: "text-align-justify", shortcut: "Ctrl+J", needs: "document", run: () => editor.setAlignment("justify")},
        {id: "bullets", title: "Lista con viñetas", section: "Párrafo", symbol: "list", shortcut: "Ctrl+Shift+8", needs: "document", run: () => editor.setList(1)},
        {id: "numbers", title: "Lista numerada", section: "Párrafo", symbol: "list-ordered", shortcut: "Ctrl+Shift+7", needs: "document", run: () => editor.setList(2)},
        {id: "indentMore", title: "Aumentar la sangría", section: "Párrafo", symbol: "list-indent-increase", shortcut: "Ctrl+M", needs: "document", run: () => editor.indentMore()},
        {id: "indentLess", title: "Reducir la sangría", section: "Párrafo", symbol: "list-indent-decrease", shortcut: "Ctrl+Shift+M", needs: "document", run: () => editor.indentLess()},
        {id: "spacing100", title: "Interlineado sencillo", section: "Párrafo", symbol: "text-align-justify", needs: "document", run: () => editor.setLineHeight(100)},
        {id: "spacing115", title: "Interlineado 1,15", section: "Párrafo", symbol: "text-align-justify", needs: "document", run: () => editor.setLineHeight(115)},
        {id: "spacing150", title: "Interlineado 1,5", section: "Párrafo", symbol: "text-align-justify", needs: "document", run: () => editor.setLineHeight(150)},
        {id: "spacing200", title: "Interlineado doble", section: "Párrafo", symbol: "text-align-justify", needs: "document", run: () => editor.setLineHeight(200)},

        {id: "table", title: "Insertar una tabla…", section: "Insertar", symbol: "table", needs: "document", enabled: () => !look.table, run: size => insertTable(size)},
        {id: "image", title: "Insertar una imagen…", section: "Insertar", symbol: "image", shortcut: "Ctrl+Shift+I", needs: "document", run: path => path ? editor.insertImage(path) : imageDialog.open()},
        {id: "link", title: "Enlace…", section: "Insertar", symbol: "link", shortcut: "Ctrl+Shift+K", needs: "document", run: href => href ? editor.setLink(href, "") : askLink()},
        {id: "removeLink", title: "Quitar el enlace", section: "Insertar", symbol: "unlink", needs: "document", enabled: () => look.link !== "", run: () => editor.removeLink()},
        {id: "pageBreak", title: "Salto de página", section: "Insertar", symbol: "separator-horizontal", shortcut: "Ctrl+Return", native: true, needs: "document", enabled: () => !look.table, run: () => editor.insertPageBreak()},
        {id: "separator", title: "Separador", section: "Insertar", symbol: "minus", needs: "document", enabled: () => !look.table, run: () => editor.insertSeparator()},
        {id: "date", title: "Fecha de hoy", section: "Insertar", symbol: "calendar", shortcut: "Ctrl+Shift+D", needs: "document", run: () => editor.insertText(App.today())},
        {id: "task", title: "Tarea pendiente", section: "Insertar", symbol: "list-todo", needs: "document", run: () => editor.insertText("☐ ")},

        {id: "rowBelow", title: "Añadir una fila debajo", section: "Tabla", symbol: "plus", needs: "table", run: () => editor.addRow(true)},
        {id: "rowAbove", title: "Añadir una fila encima", section: "Tabla", symbol: "plus", needs: "table", run: () => editor.addRow(false)},
        {id: "columnAfter", title: "Añadir una columna a la derecha", section: "Tabla", symbol: "plus", needs: "table", run: () => editor.addColumn(true)},
        {id: "columnBefore", title: "Añadir una columna a la izquierda", section: "Tabla", symbol: "plus", needs: "table", run: () => editor.addColumn(false)},
        {id: "removeRow", title: "Eliminar la fila", section: "Tabla", symbol: "minus", needs: "table", run: () => editor.removeRow()},
        {id: "removeColumn", title: "Eliminar la columna", section: "Tabla", symbol: "minus", needs: "table", run: () => editor.removeColumn()},
        {id: "removeTable", title: "Eliminar la tabla", section: "Tabla", symbol: "trash-2", needs: "table", run: () => editor.removeTable()},

        {id: "palette", title: "Órdenes", section: "Ver", symbol: "command", shortcut: "Ctrl+K", hidden: true, run: query => palette.visible ? palette.close() : palette.show(query ?? "")},
        {id: "sidebar", title: "Mostrar u ocultar el esquema", section: "Ver", symbol: "panel-left", shortcut: "Ctrl+Alt+S", needs: "document", run: () => sidebarShown = !sidebarShown},
        {id: "inspector", title: "Mostrar u ocultar el inspector", section: "Ver", symbol: "panel-right", shortcut: "Ctrl+Alt+I", needs: "document", run: tab => showInspector(tab)},
        {id: "focus", title: "Modo concentración", section: "Ver", symbol: "maximize-2", shortcut: "Ctrl+Shift+F", needs: "document", run: () => concentrating = !concentrating},
        {id: "zoomIn", title: "Acercar", section: "Ver", symbol: "zoom-in", shortcut: "Ctrl++", needs: "document", run: () => zoomTo(zoom + 0.1)},
        {id: "zoomOut", title: "Alejar", section: "Ver", symbol: "zoom-out", shortcut: "Ctrl+-", needs: "document", run: () => zoomTo(zoom - 0.1)},
        {id: "zoomReal", title: "Tamaño real (100 %)", section: "Ver", symbol: "search", shortcut: "Ctrl+1", needs: "document", run: () => zoomTo(1)},
        {id: "zoomFit", title: "Ajustar la hoja a la ventana", section: "Ver", symbol: "ruler", shortcut: "Ctrl+0", needs: "document", run: () => zoomWish = 0},
        {id: "zoom", title: "Zoom", section: "Ver", symbol: "search", hidden: true, run: percent => zoomTo(Number(percent) / 100)},

        {id: "portrait", title: "Hoja en vertical", section: "Documento", symbol: "file", needs: "document", run: () => editor.setSetup({landscape: false})},
        {id: "landscape", title: "Hoja en horizontal", section: "Documento", symbol: "file", needs: "document", run: () => editor.setSetup({landscape: true})},
        {id: "pageNumbers", title: "Números de página", section: "Documento", symbol: "file-text", needs: "document", run: () => editor.setSetup({pageNumbers: !editor.setup.pageNumbers})},
        {id: "header", title: "Encabezado y pie…", section: "Documento", symbol: "file-text", needs: "document", run: text => text ? editor.setSetup({header: text}) : showInspector("document")},
        {id: "goal", title: "Objetivo de palabras…", section: "Documento", symbol: "target", needs: "document", run: words => words ? goal = Number(words) : askGoal()},
        {id: "markdown", title: "Atajos de Markdown al escribir", section: "Documento", symbol: "code", needs: "document", run: () => editor.markdown = !editor.markdown},
        {id: "about", title: "Acerca de Textos", section: "Documento", symbol: "file-text", run: () => about.open()},

        // Only for `--do`: what a hand would do with the keyboard and the mouse.
        {id: "type", hidden: true, run: text => type(text)},
        {id: "key", hidden: true, run: name => pressNamed(name)},
        {id: "select", hidden: true, run: range => editor.select(Number(range.split("-")[0]), Number(range.split("-")[1]))},
        {id: "goto", hidden: true, run: position => editor.goTo(Number(position))},
        {id: "end", hidden: true, run: () => editor.key(Qt.Key_End, Qt.ControlModifier, "")},
        {id: "slash", hidden: true, run: query => openSlash(query ?? "")},
        {id: "context", hidden: true, run: () => context.popup(desk, 200, 160)},
        {id: "exportMenu", hidden: true, run: () => exportMenu.popup(exportButton, 0, exportButton.height + 4)},
        {id: "welcome", hidden: true, run: () => App.closeDocument()},
        {id: "asksave", hidden: true, run: () => guarded(() => App.closeDocument())},
        {id: "say", hidden: true, run: text => say(text)}
    ]

    function actionById(id) {
        for (const action of actions) {
            if (action.id === id)
                return action
        }
        return null
    }
    function usable(action) {
        if (action.needs && !App.started)
            return false
        if (action.needs === "table" && !look.table)
            return false
        return action.enabled ? action.enabled() : true
    }
    /// Runs an action by its name, with what it may need.
    function act(id, argument) {
        const action = actionById(id)
        if (!action) {
            console.warn("Textos: no hay ninguna orden «" + id + "»")
            return
        }
        if (!usable(action))
            return
        action.run(argument)
        if (!palette.visible && !slash.visible && !ask.visible && !findBar.visible && !renaming)
            typing.forceActiveFocus()
    }
    /// «verb» or «verb:argument», as `--do` writes them.
    function perform(order) {
        const colon = order.indexOf(":")
        if (colon < 0)
            act(order.trim())
        else
            act(order.slice(0, colon).trim(), order.slice(colon + 1))
    }

    // --- What the actions are made of ---------------------------------------------
    function say(text) {
        message = text
        quiet.restart()
    }
    function fresh(kind) {
        guarded(() => App.start(kind))
    }
    /// Does `next` now, or after asking what to do with the changes there are.
    function guarded(next) {
        if (App.started && editor.modified) {
            pending = next
            saveDialog.name = App.title
            saveDialog.open()
        } else {
            next()
        }
    }
    function done() {
        const next = pending
        pending = null
        if (next)
            next()
    }
    function save() {
        if (App.save())
            return true
        askWhere()
        return false
    }
    function askWhere() {
        if (App.testing)
            return
        saveAsDialog.selectedFile = App.suggestedUrl(editor.path === "" ? "odt" : editor.path.split(".").pop())
        saveAsDialog.open()
    }
    function exportAs(suffix, name, path) {
        if (path) {
            App.exportTo(path)
            return
        }
        if (App.testing)
            return
        exportDialog.suffix = suffix
        exportDialog.nameFilters = [name + " (*." + suffix + ")"]
        exportDialog.selectedFile = App.suggestedUrl(suffix)
        exportDialog.open()
    }
    function beginRename() {
        renaming = true
        nameField.text = App.title
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function insertTable(size) {
        if (size) {
            const parts = String(size).toLowerCase().split("x")
            editor.insertTable(Math.max(1, Number(parts[0]) || 3), Math.max(1, Number(parts[1]) || 3))
            return
        }
        ask.ask("Insertar una tabla", [{label: "Filas", value: "3"}, {label: "Columnas", value: "3"}], "Insertar", values => {
            const rows = Math.max(1, Math.min(200, parseInt(values[0]) || 0))
            const columns = Math.max(1, Math.min(20, parseInt(values[1]) || 0))
            editor.insertTable(rows, columns)
        })
    }
    function askLink() {
        const fields = [{label: "Dirección", value: look.link, placeholder: "https://"}]
        if (!look.selection && look.link === "")
            fields.push({label: "Texto", value: "", placeholder: "Lo que se lee (si se deja vacío, la dirección)"})
        ask.ask(look.link === "" ? "Añadir un enlace" : "Cambiar el enlace", fields, "Enlazar", values => {
            let href = values[0].trim()
            if (href === "")
                return
            if (!/^[a-z][a-z0-9+.-]*:/i.test(href))
                href = (href.includes("@") && !href.includes("/") ? "mailto:" : "https://") + href
            editor.setLink(href, values.length > 1 ? values[1] : "")
        })
    }
    function askGoal() {
        ask.ask("Objetivo de palabras", [{label: "Palabras que quieres escribir (0 para ninguno)", value: String(goal)}], "Fijar", values => {
            goal = Math.max(0, parseInt(values[0]) || 0)
        })
    }
    function showInspector(tab) {
        if (tab) {
            inspector.tab = tab
            inspectorShown = true
        } else {
            inspectorShown = !inspectorShown
        }
    }
    function zoomTo(value) {
        zoomWish = Math.max(0.4, Math.min(3, Math.round(value * 20) / 20))
    }

    // --- The keyboard -------------------------------------------------------------
    /// A key on its way to the text. False when it is nobody's.
    function press(key, modifiers, text) {
        if (key === Qt.Key_Escape) {
            if (concentrating)
                concentrating = false
            else if (findBar.visible)
                findBar.hide()
            else
                return false
            return true
        }
        // «/» where there is nothing yet offers what can go there.
        if (text === "/" && look.empty && !look.table && !(modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
            openSlash("")
            return true
        }
        blink = true
        blinking.restart()
        return editor.key(key, modifiers, text)
    }
    function type(text) {
        for (const letter of text) {
            if (letter === "¶")
                press(Qt.Key_Return, Qt.NoModifier, "\r")
            else
                press(0, Qt.NoModifier, letter)
        }
    }
    readonly property var keyNames: ({
        enter: Qt.Key_Return, tab: Qt.Key_Tab, backspace: Qt.Key_Backspace, "delete": Qt.Key_Delete, left: Qt.Key_Left, right: Qt.Key_Right,
        up: Qt.Key_Up, down: Qt.Key_Down, home: Qt.Key_Home, end: Qt.Key_End, escape: Qt.Key_Escape, pageup: Qt.Key_PageUp, pagedown: Qt.Key_PageDown
    })
    /// «ctrl+shift+left», «enter», «ctrl+b».
    function pressNamed(name) {
        const parts = name.toLowerCase().split("+")
        const last = parts.pop()
        let modifiers = Qt.NoModifier
        for (const part of parts)
            modifiers |= part === "ctrl" ? Qt.ControlModifier : part === "shift" ? Qt.ShiftModifier : part === "alt" ? Qt.AltModifier : 0
        const key = keyNames[last] ?? (last.length === 1 ? Qt.Key_A + last.charCodeAt(0) - 97 : 0)
        press(key, modifiers, "")
    }

    // --- Where things are on the sheets -------------------------------------------
    function sheetY(page) {
        return pages.originY + 24 + page * (sheetHeight + sheetGap)
    }
    function sheetX() {
        return Math.round((pages.contentWidth - sheetWidth) / 2)
    }
    /// The cursor, in the coordinates of what the sheets scroll in.
    function caretInPages() {
        const caret = editor.caretRect()
        return Qt.rect(sheetX() + caret.x * zoom, sheetY(editor.cursorPage) + caret.y * zoom, Math.max(1, caret.width * zoom), caret.height * zoom)
    }
    /// Scrolls until the cursor is in sight; at mid height when concentrating.
    function reveal() {
        if (!App.started)
            return
        const caret = caretInPages()
        const lowest = pages.originY
        const highest = Math.max(lowest, pages.originY + pages.contentHeight - pages.height)
        let y = pages.contentY
        if (concentrating)
            y = caret.y + caret.height / 2 - pages.height / 2
        else if (caret.y - 28 < y)
            y = caret.y - 28
        else if (caret.y + caret.height + 28 > y + pages.height)
            y = caret.y + caret.height + 28 - pages.height
        pages.contentY = Math.max(lowest, Math.min(highest, y))
        if (pages.contentWidth > pages.width) {
            if (caret.x - 28 < pages.contentX)
                pages.contentX = Math.max(0, caret.x - 28)
            else if (caret.x + 28 > pages.contentX + pages.width)
                pages.contentX = Math.min(pages.contentWidth - pages.width, caret.x + 28 - pages.width)
        }
    }
    function openSlash(query) {
        const caret = caretInPages()
        const at = pages.contentItem.mapToItem(win.contentItem, caret.x, caret.y + caret.height)
        slash.x = Math.max(8, Math.min(win.width - slash.width - 8, at.x))
        slash.y = at.y + 6 + 330 > win.height ? Math.max(8, at.y - caret.height - 330) : at.y + 6
        slash.show(query)
    }

    Connections {
        target: win.editor
        function onCursorMoved() {
            win.blink = true
            blinking.restart()
            Qt.callLater(win.reveal)
        }
        function onReplaced() {
            pages.contentY = pages.originY
            pages.contentX = Math.max(0, (pages.contentWidth - pages.width) / 2)
            typing.forceActiveFocus()
        }
    }
    Connections {
        target: App
        function onSaid(message) {
            win.say(message)
        }
        function onStartedChanged() {
            win.concentrating = false
            if (App.started)
                typing.forceActiveFocus()
        }
    }
    onConcentratingChanged: {
        Qt.callLater(reveal)
        if (concentrating)
            hint.restart()
    }
    onZoomChanged: Qt.callLater(reveal)

    Timer {
        id: blinking
        interval: 530
        repeat: true
        running: App.started && !App.testing && win.active
        onTriggered: win.blink = !win.blink
    }
    Timer {
        id: quiet
        interval: 4500
        onTriggered: win.message = ""
    }
    Timer {
        id: hint
        interval: 2600
    }

    // One Shortcut per action that has keys of its own.
    Repeater {
        model: win.actions.length
        Item {
            id: bound
            required property int index
            readonly property var action: win.actions[index]
            Shortcut {
                sequences: bound.action.shortcut === "Ctrl++" ? ["Ctrl++", "Ctrl+="] : [bound.action.shortcut ?? ""]
                enabled: !!bound.action.shortcut && !bound.action.native
                onActivated: win.act(bound.action.id)
            }
        }
    }

    // --- The bar ------------------------------------------------------------------
    Rectangle {
        id: bar
        width: parent.width
        height: 46
        visible: App.started && !win.concentrating
        color: Theme.chrome

        component Gap: Item {
            width: 13
            height: 26
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 16
                color: Theme.border
            }
        }
        component Tool: TButton {
            property string does
            active: {
                const action = win.actionById(does)
                return action !== null && win.usable(action)
            }
            onClicked: win.act(does)
        }

        Row {
            id: leading
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Tool {
                symbol: "panel-left"
                does: "sidebar"
                checked: win.sidebarShown
            }
            Item {
                width: Math.max(60, Math.min(name.implicitWidth + 18, tools.x - leading.x - 56))
                height: 26
                Text {
                    id: name
                    visible: !win.renaming
                    width: parent.width - 14
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 4
                    text: App.title
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                // Changes not yet in the file.
                Rectangle {
                    visible: win.editor.modified && !win.renaming
                    x: Math.min(name.implicitWidth, name.width) + 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 4
                    color: Theme.tint
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !win.renaming
                    cursorShape: Qt.IBeamCursor
                    onClicked: win.beginRename()
                }
                Field {
                    id: nameField
                    visible: win.renaming
                    width: Math.max(180, parent.width)
                    height: 26
                    z: 2
                    onAccepted: {
                        App.rename(text)
                        win.renaming = false
                        typing.forceActiveFocus()
                    }
                    onActiveFocusChanged: if (!activeFocus) win.renaming = false
                    Keys.onEscapePressed: {
                        win.renaming = false
                        typing.forceActiveFocus()
                    }
                }
            }
        }

        Row {
            id: tools
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: Math.round((leading.x + 110 - (parent.width - trailing.x)) / 2)
            spacing: 1
            Tool { symbol: "undo-2"; does: "undo" }
            Tool { symbol: "redo-2"; does: "redo" }
            Gap {}
            FCombo {
                id: styleBox
                readonly property var names: win.editor.styles.map(style => style.name)
                readonly property var ids: win.editor.styles.map(style => style.id)
                y: -1
                width: 132
                implicitWidth: 132
                model: names
                displayText: names[ids.indexOf(win.look.style)] ?? "Cuerpo"
                onActivated: index => {
                    win.editor.applyStyle(ids[index])
                    typing.forceActiveFocus()
                }
                onPressedChanged: if (pressed) currentIndex = ids.indexOf(win.look.style)
            }
            Gap {}
            Tool { symbol: "bold"; does: "bold"; checked: win.look.bold }
            Tool { symbol: "italic"; does: "italic"; checked: win.look.italic }
            Tool { symbol: "underline"; does: "underline"; checked: win.look.underline }
            Gap { visible: win.width >= 1100 }
            Tool { visible: win.width >= 1100; symbol: "text-align-start"; does: "alignLeft"; checked: win.look.alignment === "left" }
            Tool { visible: win.width >= 1100; symbol: "text-align-center"; does: "alignCenter"; checked: win.look.alignment === "center" }
            Tool { visible: win.width >= 1100; symbol: "text-align-end"; does: "alignRight"; checked: win.look.alignment === "right" }
            Tool { visible: win.width >= 1100; symbol: "text-align-justify"; does: "alignJustify"; checked: win.look.alignment === "justify" }
            Gap {}
            Tool { symbol: "list"; does: "bullets"; checked: win.look.list === 1 }
            Tool { symbol: "list-ordered"; does: "numbers"; checked: win.look.list === 2 }
            Gap {}
            Tool { symbol: "table"; does: "table" }
            Tool { symbol: "image"; does: "image" }
            Tool { symbol: "link"; does: "link"; checked: win.look.link !== "" }
        }

        Row {
            id: trailing
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            // «Órdenes»: the way to everything else.
            Rectangle {
                width: orders.implicitWidth + 44
                height: 26
                radius: 6
                color: ordersArea.containsMouse ? Theme.sideSelection : Theme.field
                Sym {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 14
                    color: Theme.dim
                }
                Text {
                    id: orders
                    x: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Órdenes"
                    color: Theme.dim
                    font.pixelSize: 12
                }
                MouseArea {
                    id: ordersArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.act("palette")
                }
            }
            Item { width: 4; height: 1 }
            TButton {
                id: exportButton
                symbol: "share"
                onClicked: exportMenu.popup(exportButton, 0, exportButton.height + 4)
            }
            Tool {
                symbol: "panel-right"
                does: "inspector"
                checked: win.inspectorShown
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // --- Outline, sheets, inspector ----------------------------------------------
    Item {
        id: body
        visible: App.started
        anchors.top: bar.visible ? bar.bottom : parent.top
        anchors.bottom: status.visible ? status.top : parent.bottom
        width: parent.width

        OutlinePanel {
            id: side
            visible: win.sidebarShown && !win.concentrating
            width: 220
            height: parent.height
            goal: win.goal
            onJump: position => {
                win.editor.goTo(position)
                typing.forceActiveFocus()
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }
        Inspector {
            id: inspector
            visible: win.inspectorShown && !win.concentrating
            anchors.right: parent.right
            width: 264
            height: parent.height
            goal: win.goal
            onAct: id => win.act(id)
            onGoalEdited: words => win.goal = words
        }
        Rectangle {
            id: desk
            anchors.left: side.visible ? side.right : parent.left
            anchors.right: inspector.visible ? inspector.left : parent.right
            height: parent.height
            color: win.concentrating ? "#ffffff" : Theme.desk
            clip: true

            ListView {
                id: pages
                anchors.fill: parent
                model: win.editor.pageCount
                contentWidth: Math.max(width, win.sheetWidth + 56)
                flickableDirection: Flickable.AutoFlickIfNeeded
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 1600
                reuseItems: false
                header: Item { width: 1; height: 24 }
                footer: Item { width: 1; height: win.concentrating ? pages.height / 2 : 6 }
                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {
                    policy: pages.contentWidth > pages.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }
                delegate: Item {
                    id: leaf
                    required property int index
                    width: pages.contentWidth
                    height: win.sheetHeight + win.sheetGap
                    // The edge of the paper, and its shadow on the desk.
                    Rectangle {
                        visible: !win.concentrating
                        x: sheet.x - 1
                        y: -1
                        width: sheet.width + 2
                        height: sheet.height + 3
                        radius: 2
                        color: Theme.dark ? "#000000" : "#1a000000"
                    }
                    PageView {
                        id: sheet
                        x: Math.round((parent.width - width) / 2)
                        width: win.sheetWidth
                        height: win.sheetHeight
                        editor: win.editor
                        page: leaf.index
                        zoom: win.zoom
                        caret: win.blink && (typing.activeFocus || App.testing)
                        dimmed: win.concentrating
                        MouseArea {
                            property int clicks: 0
                            property real last: 0
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.IBeamCursor
                            preventStealing: true
                            onPressed: mouse => {
                                typing.forceActiveFocus()
                                const x = mouse.x / win.zoom
                                const y = mouse.y / win.zoom
                                if (mouse.button === Qt.RightButton) {
                                    if (!win.look.selection)
                                        win.editor.press(leaf.index, x, y, false)
                                    context.popup()
                                    return
                                }
                                const now = Date.now()
                                clicks = now - last < 420 ? clicks + 1 : 1
                                last = now
                                if (clicks === 2) {
                                    win.editor.selectWord(leaf.index, x, y)
                                } else if (clicks >= 3) {
                                    win.editor.selectParagraph(leaf.index, x, y)
                                } else if ((mouse.modifiers & Qt.ControlModifier) && win.editor.linkAt(leaf.index, x, y) !== "") {
                                    App.openLink(win.editor.linkAt(leaf.index, x, y))
                                } else {
                                    win.editor.press(leaf.index, x, y, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                }
                            }
                            onPositionChanged: mouse => {
                                if (pressed && (mouse.buttons & Qt.LeftButton) && clicks === 1)
                                    win.editor.drag(leaf.index, mouse.x / win.zoom, mouse.y / win.zoom)
                            }
                        }
                    }
                }

                // The few things most likely wanted of a selection, by its side.
                SelectionPill {
                    id: pill
                    readonly property rect caret: {
                        win.editor.position
                        win.look
                        win.zoom
                        return win.caretInPages()
                    }
                    parent: pages.contentItem
                    z: 5
                    visible: win.look.selection && !win.look.image && !win.concentrating && App.started
                    x: Math.max(pages.contentX + 8, Math.min(pages.contentX + pages.width - width - 8, caret.x - width / 2))
                    y: caret.y - height - 10 < pages.contentY + 6 ? caret.y + caret.height + 10 : caret.y - height - 10
                    onAct: id => win.act(id)
                }
            }

            // Where the keyboard lands.
            TypingArea {
                id: typing
                anchors.fill: parent
                focus: true
                caret: {
                    win.editor.position
                    pages.contentY
                    const caret = win.caretInPages()
                    const at = pages.contentItem.mapToItem(typing, caret.x, caret.y)
                    return Qt.rect(at.x, at.y, caret.width, caret.height)
                }
                onCommitted: text => win.type(text)
                Keys.onPressed: event => event.accepted = win.press(event.key, event.modifiers, event.text)
            }

            FindBar {
                id: findBar
                anchors.right: parent.right
                anchors.rightMargin: 18
                y: 12
                z: 6
                onClosed: typing.forceActiveFocus()
            }

            // How to get out of concentration, said once and then gone.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 14
                z: 6
                width: way.implicitWidth + 24
                height: 28
                radius: 14
                color: "#cc1d1d1f"
                opacity: win.concentrating && hint.running ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 250 } }
                Text {
                    id: way
                    anchors.centerIn: parent
                    text: "Modo concentración · Esc para salir"
                    color: "#ffffff"
                    font.pixelSize: 12
                }
            }
        }
    }

    // --- The line at the bottom ---------------------------------------------------
    Rectangle {
        id: status
        visible: App.started && !win.concentrating
        anchors.bottom: parent.bottom
        width: parent.width
        height: 26
        color: Theme.chrome

        readonly property int words: win.editor.counts.words ?? 0
        function number(value) {
            return Number(value).toLocaleString(Qt.locale("es_ES"), "f", 0)
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Página " + (win.editor.cursorPage + 1) + " de " + win.editor.pageCount
                color: Theme.dim
                font.pixelSize: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (win.editor.counts.selectedCharacters ?? 0) > 0
                      ? status.number(win.editor.counts.selectedWords) + " de " + status.number(status.words) + " palabras · " + status.number(win.editor.counts.selectedCharacters) + " caracteres seleccionados"
                      : status.number(status.words) + (status.words === 1 ? " palabra · " : " palabras · ") + status.number(win.editor.counts.characters ?? 0) + " caracteres"
                color: Theme.dim
                font.pixelSize: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: status.words < 100 ? "Menos de 1 min de lectura" : Math.round(status.words / 200) + " min de lectura"
                color: Theme.dim
                font.pixelSize: 12
            }
            // How far the day's writing is from where it wants to get.
            Item {
                visible: win.goal > 0
                anchors.verticalCenter: parent.verticalCenter
                width: progress.width
                height: progress.height
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.act("goal")
                }
                Row {
                id: progress
                spacing: 7
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 84
                    height: 5
                    radius: 3
                    color: Theme.field
                    Rectangle {
                        width: Math.round(parent.width * Math.min(1, status.words / Math.max(1, win.goal)))
                        height: parent.height
                        radius: 3
                        color: status.words >= win.goal ? "#30a14e" : Theme.tint
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: status.words >= win.goal ? "Objetivo de " + status.number(win.goal) + " cumplido"
                                                   : Math.floor(100 * status.words / Math.max(1, win.goal)) + " % de " + status.number(win.goal)
                    color: Theme.dim
                    font.pixelSize: 12
                }
                }
            }
            Text {
                visible: win.message !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: win.message
                color: Theme.tint
                font.pixelSize: 12
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                visible: win.editor.lossy
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: 12
                text: "Este formato no guarda todo el aspecto"
                color: Theme.faint
                font.pixelSize: 12
            }
            TButton {
                height: 22
                size: 14
                symbol: "zoom-out"
                tone: Theme.dim
                onClicked: win.act("zoomOut")
            }
            Text {
                width: 46
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(win.zoom * 100) + " %"
                color: win.zoomWish > 0 ? Theme.text : Theme.dim
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.act("zoomFit")
                }
            }
            TButton {
                height: 22
                size: 14
                symbol: "zoom-in"
                tone: Theme.dim
                onClicked: win.act("zoomIn")
            }
        }
    }

    // --- Before there is a document -----------------------------------------------
    Welcome {
        anchors.fill: parent
        visible: !App.started
        onPicked: kind => App.start(kind)
        onOpened: path => App.open(path)
        onBrowse: win.act("open")
    }

    // --- What floats --------------------------------------------------------------
    CommandPalette {
        id: palette
        actions: win.actions.map(action => Object.assign({}, action, {enabled: () => win.usable(action)}))
        onChosen: id => win.act(id)
        onClosed: if (App.started && !ask.visible && !slash.visible && !versions.visible && !win.renaming) typing.forceActiveFocus()
    }
    SlashMenu {
        id: slash
        blocks: [
            {id: "styleH1", title: "Encabezado 1", symbol: "heading-1", hint: "#"},
            {id: "styleH2", title: "Encabezado 2", symbol: "heading-2", hint: "##"},
            {id: "styleH3", title: "Encabezado 3", symbol: "heading-3", hint: "###"},
            {id: "bullets", title: "Lista con viñetas", symbol: "list", hint: "-"},
            {id: "numbers", title: "Lista numerada", symbol: "list-ordered", hint: "1."},
            {id: "task", title: "Lista de tareas", symbol: "list-todo", hint: ""},
            {id: "styleQuote", title: "Cita", symbol: "text-quote", hint: ">"},
            {id: "styleCode", title: "Código", symbol: "code", hint: "```"},
            {id: "table", title: "Tabla", symbol: "table", hint: ""},
            {id: "image", title: "Imagen", symbol: "image", hint: ""},
            {id: "pageBreak", title: "Salto de página", symbol: "separator-horizontal", hint: ""},
            {id: "separator", title: "Separador", symbol: "minus", hint: ""},
            {id: "date", title: "Fecha de hoy", symbol: "calendar", hint: ""}
        ]
        onChosen: id => win.act(id)
        // Not a block after all: the bar and what followed it are just text.
        onLiteral: text => win.editor.insertText("/" + text)
        onClosed: if (!ask.visible) typing.forceActiveFocus()
    }
    AskDialog {
        id: ask
        onClosed: typing.forceActiveFocus()
    }
    SaveDialog {
        id: saveDialog
        onSave: {
            if (App.save()) {
                win.done()
            } else if (!App.testing) {
                win.askWhere()
            }
        }
        onDiscard: {
            // Given up on purpose: nothing of it is to come back as «recovered».
            const next = win.pending
            win.pending = null
            if (win.leaving || next === null)
                return
            win.forsake(next)
        }
        onClosed: if (App.started) typing.forceActiveFocus()
    }
    VersionsPanel {
        id: versions
        onRestore: file => App.restoreVersion(file)
        onClosed: typing.forceActiveFocus()
    }
    Popup {
        id: about
        modal: true
        anchors.centerIn: parent
        width: 320
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 12
            color: Theme.menu
            border.color: Theme.border
        }
        Overlay.modal: Rectangle {
            color: "#40000000"
        }
        contentItem: Column {
            spacing: 6
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 72
                height: 72
                sourceSize: Qt.size(144, 144)
                source: "qrc:/qt/qml/WonderlandTextos/wonderland-textos.svg"
            }
            Item { width: 1; height: 4 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Textos"
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Versión " + Qt.application.version
                color: Theme.dim
                font.pixelSize: 12
            }
            Item { width: 1; height: 4 }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                lineHeight: 1.25
                text: "El procesador de textos de Wonder OS. Escribe en OpenDocument (.odt) y entiende Word, RTF, Markdown, HTML y texto."
                color: Theme.dim
                font.pixelSize: 12
            }
        }
    }
    FMenu {
        id: exportMenu
        FMenuItem { text: "Exportar como PDF…"; onTriggered: win.act("exportPdf") }
        FSeparator {}
        FMenuItem { text: "Word (.docx)…"; onTriggered: win.act("exportDocx") }
        FMenuItem { text: "OpenDocument (.odt)…"; onTriggered: win.act("exportOdt") }
        FMenuItem { text: "Texto enriquecido (.rtf)…"; onTriggered: win.act("exportRtf") }
        FMenuItem { text: "Página web (.html)…"; onTriggered: win.act("exportHtml") }
        FMenuItem { text: "Markdown (.md)…"; onTriggered: win.act("exportMd") }
        FMenuItem { text: "Texto sin formato (.txt)…"; onTriggered: win.act("exportTxt") }
        FSeparator {}
        FMenuItem { text: "Guardar como…"; onTriggered: win.act("saveAs") }
        FMenuItem { text: "Versiones…"; onTriggered: win.act("versions") }
    }
    FMenu {
        id: context
        width: 270
        FMenuItem { text: "Cortar"; enabled: win.look.selection; onTriggered: win.act("cut") }
        FMenuItem { text: "Copiar"; enabled: win.look.selection; onTriggered: win.act("copy") }
        FMenuItem { text: "Pegar"; onTriggered: win.act("paste") }
        FSeparator {}
        FMenuItem { text: win.look.link === "" ? "Añadir un enlace…" : "Cambiar el enlace…"; onTriggered: win.act("link") }
        FMenuItem { text: "Quitar el enlace"; visible: win.look.link !== ""; onTriggered: win.act("removeLink") }
        FMenuItem { text: "Añadir una fila debajo"; visible: win.look.table; onTriggered: win.act("rowBelow") }
        FMenuItem { text: "Añadir una columna a la derecha"; visible: win.look.table; onTriggered: win.act("columnAfter") }
        FMenuItem { text: "Eliminar la fila"; visible: win.look.table; onTriggered: win.act("removeRow") }
        FMenuItem { text: "Eliminar la columna"; visible: win.look.table; onTriggered: win.act("removeColumn") }
        FSeparator {}
        FMenuItem { text: "Seleccionar todo"; onTriggered: win.act("selectAll") }
    }

    /// Drops the changes there are and goes on.
    function forsake(next) {
        App.closeDocument()
        next()
    }

    FileDialog {
        id: openDialog
        title: "Abrir un documento"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Documentos (*.odt *.docx *.rtf *.md *.markdown *.html *.htm *.txt)", "Todos los archivos (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: App.open(App.localPath(selectedFile))
    }
    FileDialog {
        id: saveAsDialog
        title: "Guardar el documento"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "odt"
        nameFilters: ["Texto OpenDocument (*.odt)", "Documento de Word (*.docx)", "Texto enriquecido (*.rtf)", "Markdown (*.md)", "Página web (*.html)", "Texto sin formato (*.txt)"]
        onAccepted: {
            if (App.saveAs(App.localPath(selectedFile)))
                win.done()
            else
                win.pending = null
        }
        onRejected: win.pending = null
    }
    FileDialog {
        id: exportDialog
        property string suffix: "pdf"
        title: "Exportar el documento"
        fileMode: FileDialog.SaveFile
        defaultSuffix: suffix
        onAccepted: App.exportTo(App.localPath(selectedFile))
    }
    FileDialog {
        id: imageDialog
        title: "Insertar una imagen"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)", "Todos los archivos (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: win.editor.insertImage(App.localPath(selectedFile))
    }

    // --- Pictures of the window, for tests ----------------------------------------
    Timer {
        interval: 250
        running: App.testing
        onTriggered: {
            for (const order of App.testActions)
                win.perform(order)
            shot.start()
        }
    }
    Timer {
        id: shot
        interval: 700
        onTriggered: App.saveShot(win)
    }
}
