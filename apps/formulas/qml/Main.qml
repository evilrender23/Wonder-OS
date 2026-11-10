import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

ApplicationWindow {
    id: win

    property bool sidebarOpen: App.setting("sidebar", true)
    property bool inspectorOpen: App.setting("inspector", true)
    /// Too narrow for both panels: the inspector stays, the sidebar gives way.
    readonly property bool narrow: width < 1100
    readonly property bool sidebarShown: sidebarOpen && !(narrow && inspectorOpen)
    /// Only the formula: no bars, no panels, no code.
    property bool concentrated: false
    /// How much larger than its size in points the formula is shown.
    property real zoom: App.setting("zoom", 2)
    /// A formula wider than the window is shown smaller, whole.
    property bool fitting: true
    readonly property bool busy: palette.visible || exportDialog.visible || versionsPanel.visible || confirmDialog.visible || nameDialog.visible
    readonly property bool bars: App.open && !concentrated
    property bool leaving: false

    width: App.testing ? App.testSize.width : App.setting("width", 1240)
    height: App.testing ? App.testSize.height : App.setting("height", 780)
    minimumWidth: 880
    minimumHeight: 520
    visible: true
    title: App.open ? App.title + " — Fórmulas" : "Fórmulas"
    color: App.glass ? "transparent" : Theme.window

    onClosing: close => {
        if (App.testing)
            return
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("sidebar", sidebarOpen)
        App.setSetting("inspector", inspectorOpen)
        App.setSetting("zoom", zoom)
        if (App.open && App.dirty && !leaving) {
            close.accepted = false
            confirmDialog.ask("¿Cerrar sin guardar?", "«" + App.title + "» tiene cambios que no se han guardado.", "Descartar", () => {
                win.leaving = true
                App.closeDocument()
                win.close()
            })
        } else if (App.open) {
            App.closeDocument()
        }
    }
    Component.onCompleted: {
        if (App.testing)
            testStep.start()
    }

    // --- Everything the app does ---------------------------------------------------
    //
    // One list: the palette shows it, the keys come from it, and `--do` runs it.
    // `always` ones work with no document open.

    function snippet(id, title, key, symbol, text) {
        return {id: id, title: title, section: "Insertar", shortcut: key, symbol: symbol, run: () => editor.insert(text)}
    }
    function copy(id, title, key, symbol, kind) {
        return {id: id, title: title, section: "Copiar como", shortcut: key, symbol: symbol, run: () => App.copyAs(kind, inspector.copyScale)}
    }
    function show(tab) {
        sidebar.tab = tab
        if (!sidebarShown)
            run("sidebar")
    }
    readonly property var actions: [
        {id: "new", title: "Nuevo documento", section: "Archivo", shortcut: "Ctrl+N", symbol: "file-plus", always: true, run: () => App.open ? App.openInNewWindow("") : App.newDocument("blank")},
        {id: "open", title: "Abrir…", section: "Archivo", shortcut: "Ctrl+O", symbol: "folder-open", always: true, run: () => openDialog.open()},
        {id: "save", title: "Guardar", section: "Archivo", shortcut: "Ctrl+S", symbol: "save", run: () => { if (!App.save()) saveDialog.ask() }},
        {id: "save-as", title: "Guardar como…", section: "Archivo", shortcut: "Ctrl+Shift+S", symbol: "save", run: () => saveDialog.ask()},
        {id: "versions", title: "Versiones…", section: "Archivo", shortcut: "", symbol: "history", run: () => versionsPanel.open()},
        {id: "export", title: "Exportar…", section: "Archivo", shortcut: "Ctrl+E", symbol: "share", run: () => exportDialog.show("png")},
        {id: "export-svg", title: "Exportar como SVG…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("svg")},
        {id: "export-pdf", title: "Exportar el documento como PDF…", section: "Archivo", shortcut: "Ctrl+P", symbol: "printer", run: () => exportDialog.show("pdf")},
        {id: "export-mathml", title: "Exportar como MathML…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("mml")},
        {id: "export-odf", title: "Exportar para LibreOffice Math (ODF)…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("odf")},
        {id: "export-tex", title: "Exportar como LaTeX…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("tex")},
        {id: "rename", title: "Cambiar el nombre del documento", section: "Archivo", shortcut: "F2", symbol: "pencil", run: () => titleBox.edit()},
        {id: "close", title: "Cerrar el documento", section: "Archivo", shortcut: "Ctrl+W", symbol: "x", run: () => win.closeDocument()},

        {id: "undo", title: "Deshacer", section: "Edición", shortcut: "Ctrl+Z", symbol: "undo-2", run: () => App.undo()},
        {id: "redo", title: "Rehacer", section: "Edición", shortcut: "Ctrl+Shift+Z|Ctrl+Y", symbol: "redo-2", run: () => App.redo()},
        {id: "complete", title: "Proponer órdenes", section: "Edición", shortcut: "Ctrl+Space", symbol: "sparkles", run: () => editor.offer()},
        {id: "edit", title: "Ir al código", section: "Edición", shortcut: "Ctrl+L", symbol: "code", run: () => editor.area.forceActiveFocus()},

        {id: "add", title: "Nueva ecuación", section: "Ecuaciones", shortcut: "Ctrl+Return|Ctrl+Enter", symbol: "plus", run: () => { App.addEquation(""); editor.area.forceActiveFocus() }},
        {id: "duplicate", title: "Duplicar la ecuación", section: "Ecuaciones", shortcut: "Ctrl+D", symbol: "copy-plus", run: () => App.duplicateEquation()},
        {id: "delete", title: "Eliminar la ecuación", section: "Ecuaciones", shortcut: "Ctrl+Shift+Backspace", symbol: "trash-2", run: () => App.removeEquation(App.current)},
        {id: "previous", title: "Ecuación anterior", section: "Ecuaciones", shortcut: "Ctrl+PgUp", symbol: "chevron-up", run: () => App.current = Math.max(0, App.current - 1)},
        {id: "next", title: "Ecuación siguiente", section: "Ecuaciones", shortcut: "Ctrl+PgDown", symbol: "chevron-down", run: () => App.current = Math.min(App.equations.length - 1, App.current + 1)},
        {id: "move-up", title: "Subir la ecuación", section: "Ecuaciones", shortcut: "Alt+Up", symbol: "arrow-up", run: () => App.moveEquation(App.current, App.current - 1)},
        {id: "move-down", title: "Bajar la ecuación", section: "Ecuaciones", shortcut: "Alt+Down", symbol: "arrow-down", run: () => App.moveEquation(App.current, App.current + 1)},

        snippet("frac", "Fracción", "Ctrl+/", "fraction", "\\frac{|}{}"),
        snippet("sqrt", "Raíz cuadrada", "Ctrl+R", "radical", "\\sqrt{|}"),
        snippet("root", "Raíz enésima", "", "radical", "\\sqrt[n]{|}"),
        snippet("sup", "Superíndice", "Ctrl+Up", "superscript", "^{|}"),
        snippet("sub", "Subíndice", "Ctrl+Down", "subscript", "_{|}"),
        snippet("sum", "Sumatorio", "", "sigma", "\\sum_{i=1}^{n} |"),
        snippet("integral", "Integral definida", "", "integral", "\\int_{a}^{b} | \\, dx"),
        snippet("limit", "Límite", "", "sigma", "\\lim_{x \\to \\infty} |"),
        snippet("matrix", "Matriz 2×2", "Ctrl+M", "grid-2x2", "\\begin{pmatrix} | & b \\\\ c & d \\end{pmatrix}"),
        snippet("cases", "Función a trozos", "", "braces", "\\begin{cases} | & \\text{si } x > 0 \\\\ 0 & \\text{si } x \\leq 0 \\end{cases}"),
        snippet("aligned", "Ecuaciones alineadas", "", "rows-3", "\\begin{aligned} | &= b \\\\ &= c \\end{aligned}"),
        snippet("paren", "Paréntesis que crecen", "Ctrl+9", "parentheses", "\\left( | \\right)"),
        snippet("binom", "Binomial", "", "parentheses", "\\binom{|}{}"),
        snippet("bold", "Negrita", "Ctrl+Shift+B", "bold", "\\mathbf{|}"),
        snippet("text", "Texto", "Ctrl+T", "type", "\\text{|}"),
        snippet("colored", "Colorear", "", "palette", "\\color{red}{|}"),

        copy("copy-latex", "Copiar como LaTeX", "Ctrl+Shift+C", "code", "latex"),
        copy("copy-mathml", "Copiar como MathML", "Ctrl+Shift+M", "braces", "mathml"),
        copy("copy-svg", "Copiar como SVG", "Ctrl+Shift+V", "file-down", "svg"),
        copy("copy-png", "Copiar como imagen PNG", "Ctrl+Shift+I", "image", "png"),

        {id: "library-save", title: "Guardar la fórmula en la biblioteca…", section: "Biblioteca", shortcut: "Ctrl+B", symbol: "star", run: () => nameDialog.ask()},
        {id: "library", title: "Ver la biblioteca", section: "Biblioteca", shortcut: "Ctrl+3", symbol: "book", run: () => win.show("library")},
        {id: "symbols", title: "Ver los símbolos", section: "Biblioteca", shortcut: "Ctrl+2", symbol: "sigma", run: () => win.show("symbols")},
        {id: "equations", title: "Ver las ecuaciones", section: "Biblioteca", shortcut: "Ctrl+1", symbol: "list-ordered", run: () => win.show("equations")},
        {id: "find-symbol", title: "Buscar un símbolo por su nombre", section: "Biblioteca", shortcut: "Ctrl+F", symbol: "search", run: () => { win.show("symbols"); sidebar.focusSearch() }},

        {id: "bigger", title: "Letra más grande", section: "Formato", shortcut: "Ctrl+Shift+.", symbol: "plus", run: () => App.size = Math.min(144, App.size + 2)},
        {id: "smaller", title: "Letra más pequeña", section: "Formato", shortcut: "Ctrl+Shift+,", symbol: "minus", run: () => App.size = Math.max(6, App.size - 2)},
        {id: "color-auto", title: "Color automático", section: "Formato", shortcut: "", symbol: "palette", run: () => App.color = ""},

        {id: "zoom-in", title: "Acercar", section: "Vista", shortcut: "Ctrl++|Ctrl+=", symbol: "zoom-in", run: () => win.setZoom(win.zoom * 1.25)},
        {id: "zoom-out", title: "Alejar", section: "Vista", shortcut: "Ctrl+-", symbol: "zoom-out", run: () => win.setZoom(win.zoom / 1.25)},
        {id: "zoom-100", title: "Tamaño real", section: "Vista", shortcut: "Ctrl+0", symbol: "search", run: () => win.setZoom(1)},
        {id: "zoom-fit", title: "Encajar a lo ancho", section: "Vista", shortcut: "", symbol: "maximize-2", run: () => win.fitting = !win.fitting},
        {id: "sidebar", title: "Barra lateral", section: "Vista", shortcut: "Ctrl+Alt+S", symbol: "panel-left", run: () => {
            if (win.sidebarShown) {
                win.sidebarOpen = false
            } else {
                win.sidebarOpen = true
                if (win.narrow)
                    win.inspectorOpen = false
            }
        }},
        {id: "inspector", title: "Inspector de formato", section: "Vista", shortcut: "Ctrl+Alt+I", symbol: "panel-right", run: () => win.inspectorOpen = !win.inspectorOpen},
        {id: "concentrate", title: "Modo concentración", section: "Vista", shortcut: "Ctrl+Shift+F", symbol: "focus", run: () => win.concentrated = !win.concentrated},
        {id: "palette", title: "Órdenes", section: "Vista", shortcut: "Ctrl+K", symbol: "command", always: true, run: () => palette.show()},
        {id: "escape", title: "Salir del modo concentración", section: "Vista", shortcut: "Escape", symbol: "x", run: () => win.concentrated = false}
    ]

    function run(id) {
        const action = actions.find(a => a.id === id)
        if (!action || (!App.open && !action.always))
            return false
        action.run()
        return true
    }
    function setZoom(value) {
        zoom = Math.max(0.5, Math.min(8, Math.round(value * 100) / 100))
    }
    function closeDocument() {
        const leave = () => App.closeDocument()
        if (App.dirty)
            confirmDialog.ask("¿Cerrar sin guardar?", "«" + App.title + "» tiene cambios que no se han guardado.", "Descartar", leave)
        else
            leave()
    }
    /// A formula of the library: into the equation in hand when it is empty, or as a new one.
    function use(latex) {
        if (App.latex.trim() === "")
            App.latex = latex
        else
            App.addEquation(latex)
        App.breakRun()
    }

    Instantiator {
        model: win.actions.filter(a => a.shortcut !== "")
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.shortcut.split("|")
            enabled: !win.busy && (App.open || modelData.always === true) && (modelData.id !== "escape" || win.concentrated)
            onActivated: win.run(modelData.id)
        }
    }

    Connections {
        target: App
        function onSaid(message) {
            toast.say(message)
        }
    }

    // --- The window ------------------------------------------------------------------

    Welcome {
        id: welcome
        anchors.fill: parent
        visible: !App.open
        onOpenRequested: openDialog.open()
    }

    Item {
        anchors.fill: parent
        visible: App.open

        // The bar.
        Rectangle {
            id: bar
            readonly property bool roomy: width >= 960
            width: parent.width
            height: win.bars ? 46 : 0
            visible: win.bars
            color: Theme.chrome
            z: 3
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
            Row {
                id: barLeft
                x: 10
                height: parent.height - 1
                spacing: 6
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "panel-left"
                    checked: win.sidebarShown
                    onClicked: win.run("sidebar")
                }
                Item {
                    id: titleBox
                    function edit() {
                        titleField.text = App.title
                        titleField.visible = true
                        titleField.forceActiveFocus()
                        titleField.selectAll()
                    }
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(Math.max(titleText.implicitWidth, 60), Math.max(90, barTools.x - 80)) + 28
                    height: 28
                    Rectangle {
                        anchors.fill: parent
                        radius: 7
                        color: titleHit.containsMouse && !titleField.visible ? Theme.hover : "transparent"
                    }
                    Text {
                        id: titleText
                        x: 8
                        width: parent.width - 28
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !titleField.visible
                        elide: Text.ElideRight
                        text: App.title
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    // Unsaved changes.
                    Rectangle {
                        x: Math.min(titleText.x + titleText.implicitWidth, parent.width - 20) + 7
                        anchors.verticalCenter: parent.verticalCenter
                        visible: App.dirty && !titleField.visible
                        width: 7
                        height: 7
                        radius: 4
                        color: Theme.tint
                    }
                    MouseArea {
                        id: titleHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: titleBox.edit()
                    }
                    Field {
                        id: titleField
                        visible: false
                        anchors.fill: parent
                        font.weight: Font.DemiBold
                        onEditingFinished: {
                            if (visible && text.trim() !== "")
                                App.title = text.trim()
                            visible = false
                        }
                        Keys.onEscapePressed: visible = false
                    }
                }
            }
            component Split: Item {
                width: 13
                height: 26
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Theme.border
                }
            }
            component Tool: TButton {
                property string action
                onClicked: win.run(action)
            }
            Row {
                id: barTools
                // In the middle of the bar, unless the title or the right end need the room.
                x: Math.max(barLeft.x + barLeft.width + 10, Math.min(Math.round((bar.width - width) / 2), barRight.x - width - 12))
                height: parent.height - 1
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    Tool { symbol: "undo-2"; action: "undo"; active: App.canUndo }
                    Tool { symbol: "redo-2"; action: "redo"; active: App.canRedo }
                    Split {}
                    Tool { symbol: "plus"; action: "add" }
                    Tool { symbol: "copy-plus"; action: "duplicate" }
                    Tool { symbol: "trash-2"; action: "delete" }
                    Split {}
                    // The structures are also in the symbols panel: the first to give way in a narrow window.
                    Tool { visible: bar.roomy; symbol: "fraction"; action: "frac" }
                    Tool { visible: bar.roomy; symbol: "radical"; action: "sqrt" }
                    Tool { visible: bar.roomy; symbol: "superscript"; action: "sup" }
                    Tool { visible: bar.roomy; symbol: "subscript"; action: "sub" }
                    Tool { visible: bar.roomy; symbol: "sigma"; action: "sum" }
                    Tool { visible: bar.roomy; symbol: "integral"; action: "integral" }
                    Tool { visible: bar.roomy; symbol: "grid-2x2"; action: "matrix" }
                    Split { visible: bar.roomy }
                    Tool { symbol: "star"; action: "library-save" }
                }
            }
            Row {
                id: barRight
                x: parent.width - width - 10
                height: parent.height - 1
                spacing: 2
                Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "command"; action: "palette" }
                TButton {
                    id: copyButton
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "copy"
                    onClicked: copyMenu.popup(copyButton, 0, copyButton.height + 4)
                }
                Tool { anchors.verticalCenter: parent.verticalCenter; symbol: "share"; action: "export" }
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "panel-right"
                    checked: win.inspectorOpen
                    onClicked: win.run("inspector")
                }
            }
        }

        Sidebar {
            id: sidebar
            y: bar.height
            width: 264
            height: parent.height - bar.height - status.height
            visible: win.bars && win.sidebarShown
            onInsertRequested: snippet => editor.insert(snippet)
            onUseRequested: latex => win.use(latex)
            onSaveRequested: win.run("library-save")
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }
        Inspector {
            id: inspector
            x: parent.width - width
            y: bar.height
            width: 262
            height: parent.height - bar.height - status.height
            visible: win.bars && win.inspectorOpen
            onRun: action => win.run(action)
            onGoTo: position => editor.goTo(position)
        }

        // The formula, and under it its code.
        Item {
            id: stage
            x: sidebar.visible ? sidebar.width : 0
            y: bar.height
            width: parent.width - x - (inspector.visible ? inspector.width : 0)
            height: parent.height - bar.height - status.height

            Rectangle {
                id: preview
                width: parent.width
                height: parent.height - codeBox.height
                color: Theme.canvas
                Flickable {
                    id: view
                    anchors.fill: parent
                    contentWidth: Math.max(width, formula.implicitWidth + 96)
                    contentHeight: Math.max(height, formula.implicitHeight + 72)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    FormulaItem {
                        id: formula
                        x: Math.round((view.contentWidth - width) / 2)
                        y: Math.round((view.contentHeight - height) / 2)
                        latex: App.latex
                        family: App.font
                        size: App.size * 96 / 72 * win.zoom
                        fit: win.fitting ? view.width - 96 : 0
                        color: App.color === "" ? Theme.text : App.color
                        // What the cursor is in shows in the formula.
                        mark: editor.area.activeFocus && !editor.selecting ? editor.area.cursorPosition : -1
                        markColor: Theme.dark ? "#408e8cff" : "#285b5bd6"
                        TapHandler {
                            // A press on a part of the formula takes the cursor to its code.
                            onTapped: point => {
                                const at = formula.sourceAt(point.position.x, point.position.y)
                                if (at >= 0)
                                    editor.goTo(at)
                            }
                        }
                    }
                }
                Text {
                    visible: App.latex.trim() === ""
                    anchors.centerIn: parent
                    text: "La fórmula aparecerá aquí"
                    color: Theme.faint
                    font.pixelSize: 15
                }
                // Its number in the document, where LaTeX puts it.
                Text {
                    visible: App.equations.length > 1 && !win.concentrated
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "(" + (App.current + 1) + ")"
                    color: Theme.dim
                    font.family: "serif"
                    font.pixelSize: 18
                }

                // Out of the concentration mode.
                Rectangle {
                    visible: win.concentrated
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    width: 34
                    height: 30
                    radius: 8
                    color: leaveHit.containsMouse ? Theme.hover : "transparent"
                    Sym {
                        anchors.centerIn: parent
                        name: "minimize-2"
                        color: Theme.dim
                    }
                    MouseArea {
                        id: leaveHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.concentrated = false
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
                    anchors.bottomMargin: 22
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

            Item {
                id: codeBox
                y: preview.height
                width: parent.width
                // With everything else out of the way the code stays, lower: it is what is being written.
                height: win.concentrated ? 132 : Math.max(150, Math.min(300, Math.round(parent.height * 0.36)))
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.border
                    z: 2
                }
                // What the code is, and what is wrong with it.
                Rectangle {
                    id: codeHead
                    y: 1
                    width: parent.width
                    height: 30
                    color: Theme.window
                    Text {
                        x: 18
                        anchors.verticalCenter: parent.verticalCenter
                        text: "LaTeX"
                        color: Theme.dim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        id: problem
                        readonly property var first: App.errors.length > 0 ? App.errors[0] : null
                        visible: first !== null
                        x: 66
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(parent.width - x - 14, problemText.implicitWidth + 34)
                        height: 22
                        radius: 11
                        color: Theme.dark ? "#33ff6961" : "#1ad70015"
                        Rectangle {
                            x: 9
                            anchors.verticalCenter: parent.verticalCenter
                            width: 7
                            height: 7
                            radius: 4
                            color: Theme.dark ? "#ff6961" : "#d70015"
                        }
                        Text {
                            id: problemText
                            x: 23
                            width: parent.width - 32
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: problem.first ? "Col. " + problem.first.column + ": " + problem.first.message + (App.errors.length > 1 ? "  (+" + (App.errors.length - 1) + ")" : "") : ""
                            color: Theme.dark ? "#ff8a84" : "#b00012"
                            font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: editor.goTo(problem.first.position)
                        }
                    }
                }
                Editor {
                    id: editor
                    y: codeHead.y + codeHead.height
                    width: parent.width
                    height: parent.height - y
                    onUndoRequested: win.run("undo")
                    onRedoRequested: win.run("redo")
                }
            }

            // What is most likely wanted of what is selected in the code.
            Rectangle {
                id: pill
                readonly property rect at: editor.selectionRect
                visible: editor.selecting && !win.concentrated
                x: Math.max(8, Math.min(stage.width - width - 8, at.x + at.width / 2 - width / 2))
                y: codeBox.y + editor.y + at.y - height - 8
                z: 6
                width: pillRow.width + 10
                height: 34
                radius: 10
                color: Theme.floating
                border.color: Theme.floatingBorder
                Row {
                    id: pillRow
                    x: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Tool { symbol: "fraction"; action: "frac" }
                    Tool { symbol: "radical"; action: "sqrt" }
                    Tool { symbol: "superscript"; action: "sup" }
                    Tool { symbol: "subscript"; action: "sub" }
                    Tool { symbol: "parentheses"; action: "paren" }
                    Tool { symbol: "bold"; action: "bold" }
                    Tool { symbol: "type"; action: "text" }
                    Tool { symbol: "palette"; action: "colored" }
                }
            }
        }

        // The line at the bottom.
        Rectangle {
            id: status
            y: parent.height - height
            width: parent.width
            height: win.bars ? 26 : 0
            visible: win.bars
            color: Theme.chrome
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Row {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                component Datum: Text {
                    color: Theme.dim
                    font.pixelSize: 11
                }
                Datum { text: "Ecuación " + (App.current + 1) + " de " + App.equations.length }
                Datum { text: App.latex.length === 1 ? "1 carácter" : App.latex.length + " caracteres" }
                Datum {
                    text: App.errors.length === 0 ? "Sin avisos" : (App.errors.length === 1 ? "1 aviso" : App.errors.length + " avisos")
                    color: App.errors.length === 0 ? Theme.dim : (Theme.dark ? "#ff8a84" : "#b00012")
                }
                Datum { text: App.fontInUse }
                Datum { text: Math.round(App.size) + " pt" }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                TButton {
                    width: 24
                    height: 20
                    size: 12
                    symbol: "maximize-2"
                    checked: win.fitting
                    onClicked: win.run("zoom-fit")
                }
                TButton {
                    width: 24
                    height: 20
                    size: 12
                    symbol: "minus"
                    onClicked: win.run("zoom-out")
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 46
                    horizontalAlignment: Text.AlignHCenter
                    // What is seen, which is less than what was asked when the formula had to shrink.
                    text: Math.round(win.zoom * formula.shrink * 100) + " %"
                    color: Theme.dim
                    font.pixelSize: 11
                    MouseArea {
                        anchors.fill: parent
                        onClicked: win.run("zoom-100")
                    }
                }
                TButton {
                    width: 24
                    height: 20
                    size: 12
                    symbol: "plus"
                    onClicked: win.run("zoom-in")
                }
            }
        }
    }

    // --- Menus and dialogs -----------------------------------------------------------

    FMenu {
        id: copyMenu
        FMenuItem { text: "Copiar como imagen PNG"; onTriggered: win.run("copy-png") }
        FMenuItem { text: "Copiar como SVG"; onTriggered: win.run("copy-svg") }
        FSeparator {}
        FMenuItem { text: "Copiar como LaTeX"; onTriggered: win.run("copy-latex") }
        FMenuItem { text: "Copiar como MathML"; onTriggered: win.run("copy-mathml") }
    }
    CommandPalette {
        id: palette
        host: win
    }
    ConfirmDialog {
        id: confirmDialog
    }
    NameDialog {
        id: nameDialog
        onAccepted: name => {
            App.saveToLibrary(name)
            win.show("library")
        }
    }
    ExportDialog {
        id: exportDialog
        onAccepted: (format, scale, transparent) => {
            exportFile.format = format
            exportFile.scale = scale
            exportFile.transparent = transparent
            exportFile.nameFilters = [format.toUpperCase() + " (*." + format + ")"]
            exportFile.selectedFile = App.suggestedUrl(format)
            exportFile.open()
        }
    }
    VersionsPanel {
        id: versionsPanel
    }
    FileDialog {
        id: openDialog
        title: "Abrir un documento de fórmulas"
        nameFilters: ["Fórmulas (*.wform *.odf *.tex *.mml)", "Fórmula de Wonderland (*.wform)", "Fórmula de OpenDocument (*.odf)", "LaTeX (*.tex)", "MathML (*.mml)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            // One document per window: with one open, the other gets its own.
            if (App.open)
                App.openInNewWindow(App.localPath(selectedFile))
            else
                App.openUrl(selectedFile)
        }
    }
    FileDialog {
        id: saveDialog
        function ask() {
            selectedFile = App.suggestedUrl("wform")
            open()
        }
        title: "Guardar el documento"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wform"
        nameFilters: ["Fórmula de Wonderland (*.wform)", "Fórmula de OpenDocument (*.odf)", "LaTeX (*.tex)"]
        onAccepted: App.saveAs(selectedFile)
    }
    FileDialog {
        id: exportFile
        property string format: "png"
        property real scale: 2
        property bool transparent: true
        title: "Exportar"
        fileMode: FileDialog.SaveFile
        defaultSuffix: format
        onAccepted: App.exportTo(selectedFile, format, scale, transparent)
    }
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            for (const url of drop.urls) {
                if (!App.open)
                    App.openUrl(url)
                else
                    App.openInNewWindow(App.localPath(url))
            }
        }
    }

    // --- Pictures for the tests (--shot, --do) --------------------------------------
    //
    // Every action of the registry is a verb; these are the ones that take a word.

    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        if (run(action))
            return
        switch (verb) {
        case "sample":
            App.newDocument("sample")
            break
        case "template":
            App.newDocument(arg)
            break
        case "open":
            App.openPath(arg)
            break
        case "save":
            App.saveAs("file://" + arg)
            break
        case "export": {
            // export:<format>:<path>[:<scale>]
            const parts = arg.split(":")
            App.exportTo("file://" + parts[1], parts[0], parts.length > 2 ? Number(parts[2]) : 2, true)
            break
        }
        case "welcome":
            App.closeDocument()
            break
        case "scroll":
            welcome.scrollTo(Number(arg))
            break
        case "equation":
            App.current = Number(arg) - 1
            break
        case "new-equation":
            App.addEquation(arg)
            break
        case "latex":
            App.latex = arg
            App.breakRun()
            break
        case "type":
            // As the keys would: at the cursor, with the completions it brings.
            editor.area.insert(editor.area.cursorPosition, arg)
            break
        case "accept":
            editor.complete()
            break
        case "insert":
            editor.insert(arg)
            break
        case "cursor":
            editor.area.cursorPosition = Number(arg)
            editor.area.forceActiveFocus()
            break
        case "select": {
            const range = arg.split(",").map(Number)
            editor.area.select(range[0], range[1])
            break
        }
        case "tab":
            win.show(arg)
            break
        case "search":
            sidebar.searchFor(arg)
            break
        case "keep":
            App.saveToLibrary(arg)
            break
        case "title":
            App.title = arg
            break
        case "size":
            App.size = Number(arg)
            break
        case "color":
            App.color = arg
            break
        case "font":
            App.font = arg
            break
        case "zoom":
            win.setZoom(Number(arg) / 100)
            break
        case "copy":
            App.copyAs(arg, inspector.copyScale)
            break
        case "copy-menu":
            copyMenu.popup(copyButton, 0, copyButton.height + 4)
            break
        case "export-dialog":
            exportDialog.show(arg === "" ? "png" : arg)
            break
        case "palette-query":
            palette.show(arg)
            break
        case "restore": {
            const list = App.versions
            const index = Number(arg)
            if (index < list.length)
                App.restoreVersion(list[index].path)
            break
        }
        case "recover":
            if (App.recoveries.length > 0)
                App.recover(App.recoveries[0].path)
            break
        case "say":
            toast.say(arg)
            break
        default:
            console.warn("unknown action:", action)
        }
    }
    Timer {
        id: testStep
        property int at: 0
        interval: 300
        // One at a time, so that each sees what the one before left.
        onTriggered: {
            if (at < App.testActions.length) {
                win.runAction(App.testActions[at].trim())
                ++at
                interval = 60
                restart()
            } else {
                testShot.start()
            }
        }
    }
    Timer {
        id: testShot
        interval: 900
        onTriggered: App.saveShot(win)
    }
}
