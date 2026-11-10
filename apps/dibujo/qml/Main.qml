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
    property bool minimapOpen: App.setting("minimap", true)
    /// Only the canvas: no bars, no panels.
    property bool concentrated: false
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool busy: palette.visible || exportDialog.visible || versionsPanel.visible || confirmDialog.visible
    readonly property bool bars: App.open && !concentrated
    readonly property var ed: App.editor
    readonly property var info: App.editor.info
    readonly property bool some: App.editor.selection.length > 0

    width: App.testing ? App.testSize.width : App.setting("width", 1240)
    height: App.testing ? App.testSize.height : App.setting("height", 780)
    minimumWidth: 880
    minimumHeight: 520
    visible: true
    title: App.open ? ed.title + " — Dibujo" : "Dibujo"
    color: App.glass ? "transparent" : Theme.window

    onClosing: close => {
        if (App.testing)
            return
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("sidebar", sidebarOpen)
        App.setSetting("inspector", inspectorOpen)
        App.setSetting("minimap", minimapOpen)
        if (App.open && ed.dirty && !leaving) {
            close.accepted = false
            confirmDialog.ask("¿Cerrar sin guardar?", "«" + ed.title + "» tiene cambios que no se han guardado.", "Descartar", () => {
                win.leaving = true
                App.closeDocument()
                win.close()
            })
        } else if (App.open) {
            App.closeDocument()
        }
    }
    property bool leaving: false
    Component.onCompleted: {
        ed.darkCanvas = Qt.binding(() => Theme.dark)
        if (App.testing)
            testStep.start()
    }

    // --- Everything the app does ---------------------------------------------------
    //
    // One list: the palette shows it, the keys come from it, and `--do` runs it.
    // `plain` keys are letters and the like, which a text field must get first;
    // `always` ones work with no drawing open.

    function tool(id, title, key, symbol) {
        return {id: "tool-" + id.replace("form:", ""), title: title, section: "Herramientas", shortcut: key, symbol: symbol, plain: true, run: () => ed.tool = id}
    }
    readonly property var actions: [
        {id: "new", title: "Nuevo dibujo", section: "Archivo", shortcut: "Ctrl+N", symbol: "file-plus", always: true, run: () => App.open ? App.openInNewWindow("") : App.newDocument("blank")},
        {id: "open", title: "Abrir…", section: "Archivo", shortcut: "Ctrl+O", symbol: "folder-open", always: true, run: () => openDialog.open()},
        {id: "save", title: "Guardar", section: "Archivo", shortcut: "Ctrl+S", symbol: "save", run: () => { if (!App.save()) saveDialog.ask() }},
        {id: "save-as", title: "Guardar como…", section: "Archivo", shortcut: "Ctrl+Shift+S", symbol: "save", run: () => saveDialog.ask()},
        {id: "versions", title: "Versiones…", section: "Archivo", shortcut: "", symbol: "history", run: () => versionsPanel.open()},
        {id: "export", title: "Exportar…", section: "Archivo", shortcut: "Ctrl+E", symbol: "share", run: () => exportDialog.show("png")},
        {id: "export-svg", title: "Exportar como SVG…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("svg")},
        {id: "export-pdf", title: "Exportar como PDF…", section: "Archivo", shortcut: "", symbol: "file-down", run: () => exportDialog.show("pdf")},
        {id: "import-svg", title: "Importar SVG…", section: "Archivo", shortcut: "", symbol: "upload", run: () => importDialog.open()},
        {id: "rename", title: "Cambiar el nombre del dibujo", section: "Archivo", shortcut: "", symbol: "pencil", run: () => titleBox.edit()},
        {id: "close", title: "Cerrar el dibujo", section: "Archivo", shortcut: "Ctrl+W", symbol: "x", run: () => win.closeDrawing()},

        {id: "undo", title: "Deshacer", section: "Edición", shortcut: "Ctrl+Z", symbol: "undo-2", plain: true, run: () => ed.undo()},
        {id: "redo", title: "Rehacer", section: "Edición", shortcut: "Ctrl+Shift+Z|Ctrl+Y", symbol: "redo-2", plain: true, run: () => ed.redo()},
        {id: "cut", title: "Cortar", section: "Edición", shortcut: "Ctrl+X", symbol: "scissors", plain: true, run: () => ed.cut()},
        {id: "copy", title: "Copiar", section: "Edición", shortcut: "Ctrl+C", symbol: "copy", plain: true, run: () => ed.copy()},
        {id: "paste", title: "Pegar", section: "Edición", shortcut: "Ctrl+V", symbol: "clipboard", plain: true, run: () => ed.paste()},
        {id: "duplicate", title: "Duplicar", section: "Edición", shortcut: "Ctrl+D", symbol: "copy-plus", run: () => ed.duplicate()},
        {id: "delete", title: "Eliminar", section: "Edición", shortcut: "Delete|Backspace", symbol: "trash-2", plain: true, run: () => ed.deleteSelection()},
        {id: "select-all", title: "Seleccionar todo", section: "Edición", shortcut: "Ctrl+A", symbol: "square-dashed", plain: true, run: () => ed.selectAll()},
        {id: "deselect", title: "Quitar la selección", section: "Edición", shortcut: "Ctrl+Shift+A", symbol: "square-dashed", run: () => ed.clearSelection()},
        {id: "edit-text", title: "Editar el texto", section: "Edición", shortcut: "F2", symbol: "type", run: () => { if (win.info.single) typer.begin(win.info.id) }},

        tool("select", "Seleccionar", "V", "mouse-pointer-2"),
        tool("hand", "Mano", "H", "hand"),
        tool("rect", "Rectángulo", "R", "square"),
        tool("ellipse", "Elipse", "O", "circle"),
        tool("line", "Línea", "L", "slash"),
        tool("arrow", "Flecha", "A", "move-up-right"),
        tool("connector", "Conector", "C", "workflow"),
        tool("polygon", "Polígono", "G", "pentagon"),
        tool("star", "Estrella", "S", "star"),
        tool("pen", "Pluma", "P", "pen-tool"),
        tool("pencil", "Lápiz", "D", "pencil"),
        tool("text", "Texto", "T", "type"),
        tool("note", "Nota adhesiva", "N", "sticky-note"),
        tool("frame", "Marco", "F", "proportions"),
        {id: "image", title: "Insertar imagen…", section: "Herramientas", shortcut: "I", symbol: "image", plain: true, run: () => imageDialog.open()},
        {id: "frame-a4", title: "Marco A4 vertical", section: "Marcos", shortcut: "", symbol: "proportions", run: () => ed.addFrame("a4")},
        {id: "frame-a4h", title: "Marco A4 horizontal", section: "Marcos", shortcut: "", symbol: "proportions", run: () => ed.addFrame("a4h")},
        {id: "frame-16:9", title: "Marco de pantalla 16:9", section: "Marcos", shortcut: "", symbol: "presentation", run: () => ed.addFrame("16:9")},
        {id: "frame-square", title: "Marco cuadrado", section: "Marcos", shortcut: "", symbol: "square", run: () => ed.addFrame("square")},

        {id: "group", title: "Agrupar", section: "Organizar", shortcut: "Ctrl+G", symbol: "group", run: () => ed.group()},
        {id: "ungroup", title: "Desagrupar", section: "Organizar", shortcut: "Ctrl+Shift+G", symbol: "ungroup", run: () => ed.ungroup()},
        {id: "front", title: "Traer al frente", section: "Organizar", shortcut: "Ctrl+Shift+]", symbol: "bring-to-front", run: () => ed.reorder("front")},
        {id: "forward", title: "Subir un nivel", section: "Organizar", shortcut: "Ctrl+]", symbol: "chevron-up", run: () => ed.reorder("forward")},
        {id: "backward", title: "Bajar un nivel", section: "Organizar", shortcut: "Ctrl+[", symbol: "chevron-down", run: () => ed.reorder("backward")},
        {id: "back", title: "Enviar al fondo", section: "Organizar", shortcut: "Ctrl+Shift+[", symbol: "send-to-back", run: () => ed.reorder("back")},
        {id: "lock", title: "Bloquear o desbloquear", section: "Organizar", shortcut: "Ctrl+L", symbol: "lock", run: () => ed.toggleLock()},
        {id: "align-left", title: "Alinear a la izquierda", section: "Organizar", shortcut: "", symbol: "align-start-vertical", run: () => ed.align("left")},
        {id: "align-hcenter", title: "Centrar en horizontal", section: "Organizar", shortcut: "", symbol: "align-center-vertical", run: () => ed.align("hcenter")},
        {id: "align-right", title: "Alinear a la derecha", section: "Organizar", shortcut: "", symbol: "align-end-vertical", run: () => ed.align("right")},
        {id: "align-top", title: "Alinear arriba", section: "Organizar", shortcut: "", symbol: "align-start-horizontal", run: () => ed.align("top")},
        {id: "align-vcenter", title: "Centrar en vertical", section: "Organizar", shortcut: "", symbol: "align-center-horizontal", run: () => ed.align("vcenter")},
        {id: "align-bottom", title: "Alinear abajo", section: "Organizar", shortcut: "", symbol: "align-end-horizontal", run: () => ed.align("bottom")},
        {id: "distribute-h", title: "Repartir en horizontal", section: "Organizar", shortcut: "", symbol: "align-horizontal-distribute-center", run: () => ed.distribute("h")},
        {id: "distribute-v", title: "Repartir en vertical", section: "Organizar", shortcut: "", symbol: "align-vertical-distribute-center", run: () => ed.distribute("v")},
        {id: "nudge-left", title: "Mover a la izquierda", section: "Organizar", shortcut: "Left", symbol: "move-right", plain: true, run: () => ed.nudge(-1, 0)},
        {id: "nudge-right", title: "Mover a la derecha", section: "Organizar", shortcut: "Right", symbol: "move-right", plain: true, run: () => ed.nudge(1, 0)},
        {id: "nudge-up", title: "Mover arriba", section: "Organizar", shortcut: "Up", symbol: "move-right", plain: true, run: () => ed.nudge(0, -1)},
        {id: "nudge-down", title: "Mover abajo", section: "Organizar", shortcut: "Down", symbol: "move-right", plain: true, run: () => ed.nudge(0, 1)},
        {id: "nudge-left-10", title: "Mover 10 a la izquierda", section: "Organizar", shortcut: "Shift+Left", symbol: "move-right", plain: true, run: () => ed.nudge(-10, 0)},
        {id: "nudge-right-10", title: "Mover 10 a la derecha", section: "Organizar", shortcut: "Shift+Right", symbol: "move-right", plain: true, run: () => ed.nudge(10, 0)},
        {id: "nudge-up-10", title: "Mover 10 arriba", section: "Organizar", shortcut: "Shift+Up", symbol: "move-right", plain: true, run: () => ed.nudge(0, -10)},
        {id: "nudge-down-10", title: "Mover 10 abajo", section: "Organizar", shortcut: "Shift+Down", symbol: "move-right", plain: true, run: () => ed.nudge(0, 10)},

        {id: "zoom-100", title: "Zoom al 100 %", section: "Vista", shortcut: "1", symbol: "search", plain: true, run: () => ed.setZoom(1)},
        {id: "zoom-fit", title: "Encajar todo", section: "Vista", shortcut: "0", symbol: "maximize", plain: true, run: () => ed.fitAll()},
        {id: "zoom-selection", title: "Encajar la selección", section: "Vista", shortcut: "Shift+1|!", symbol: "minimize", plain: true, run: () => ed.fitSelection()},
        {id: "zoom-in", title: "Acercar", section: "Vista", shortcut: "Ctrl++|Ctrl+=|+", symbol: "zoom-in", plain: true, run: () => ed.zoomIn()},
        {id: "zoom-out", title: "Alejar", section: "Vista", shortcut: "Ctrl+-|-", symbol: "zoom-out", plain: true, run: () => ed.zoomOut()},
        {id: "grid", title: "Mostrar u ocultar la rejilla", section: "Vista", shortcut: "Ctrl+'", symbol: "grid-3x3", run: () => ed.grid = !ed.grid},
        {id: "snap", title: "Activar o desactivar el imán", section: "Vista", shortcut: "Ctrl+Shift+'", symbol: "magnet", run: () => ed.snap = !ed.snap},
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
        {id: "layers", title: "Ver las capas", section: "Vista", shortcut: "", symbol: "layers", run: () => { if (!win.sidebarShown) win.run("sidebar"); sidebar.tab = "layers" }},
        {id: "shapes", title: "Ver la biblioteca de formas", section: "Vista", shortcut: "", symbol: "shapes", run: () => { if (!win.sidebarShown) win.run("sidebar"); sidebar.tab = "shapes" }},
        {id: "minimap", title: "Minimapa", section: "Vista", shortcut: "Ctrl+M", symbol: "map", run: () => win.minimapOpen = !win.minimapOpen},
        {id: "concentrate", title: "Modo concentración", section: "Vista", shortcut: "Ctrl+Shift+F", symbol: "focus", run: () => win.concentrated = !win.concentrated},
        {id: "palette", title: "Órdenes", section: "Vista", shortcut: "Ctrl+K", symbol: "command", always: true, run: () => palette.show()},
        {id: "escape", title: "Soltar lo que hay en la mano", section: "Vista", shortcut: "Escape", symbol: "x", plain: true, run: () => win.concentrated ? win.concentrated = false : ed.escape()},
        {id: "finish", title: "Terminar el trazo de la pluma", section: "Herramientas", shortcut: "Return|Enter", symbol: "check", plain: true, run: () => ed.tool === "pen" ? ed.finishPen(false) : (win.info.single ? typer.begin(win.info.id) : null)}
    ]

    function run(id) {
        const action = actions.find(a => a.id === id)
        if (!action || (!App.open && !action.always))
            return false
        action.run()
        return true
    }
    function closeDrawing() {
        const leave = () => App.closeDocument()
        if (ed.dirty)
            confirmDialog.ask("¿Cerrar sin guardar?", "«" + ed.title + "» tiene cambios que no se han guardado.", "Descartar", leave)
        else
            leave()
    }

    Instantiator {
        model: win.actions.filter(a => a.shortcut !== "")
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.shortcut.split("|")
            enabled: !win.busy && (App.open || modelData.always === true) && !(modelData.plain === true && win.typing)
            onActivated: win.run(modelData.id)
        }
    }

    Connections {
        target: App
        function onSaid(message) {
            toast.say(message)
        }
    }
    Connections {
        target: App.editor
        function onEditRequested(id) {
            typer.begin(id)
        }
        function onViewChanged() {
            typer.place()
        }
    }

    // --- The window ------------------------------------------------------------------

    Welcome {
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
                        titleField.text = ed.title
                        titleField.visible = true
                        titleField.forceActiveFocus()
                        titleField.selectAll()
                    }
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(Math.max(titleText.implicitWidth, 60), Math.max(90, (bar.width - barTools.width) / 2 - 90)) + 28
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
                        text: ed.title
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    // Unsaved changes.
                    Rectangle {
                        x: Math.min(titleText.x + titleText.implicitWidth, parent.width - 20) + 7
                        anchors.verticalCenter: parent.verticalCenter
                        visible: ed.dirty && !titleField.visible
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
                                ed.title = text.trim()
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
                    height: 16
                    color: Theme.border
                }
            }
            Row {
                id: barTools
                // In the middle, unless the title needs the room.
                x: Math.max(barLeft.x + barLeft.width + 12, Math.min(Math.round((parent.width - width) / 2), barRight.x - width - 12))
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                TButton { symbol: "undo-2"; active: ed.canUndo; onClicked: win.run("undo") }
                TButton { symbol: "redo-2"; active: ed.canRedo; onClicked: win.run("redo") }
                Split {}
                TButton {
                    id: frameButton
                    symbol: "proportions"
                    onClicked: frameMenu.popup(frameButton, 0, frameButton.height + 4)
                }
                TButton { symbol: "image"; onClicked: win.run("image") }
                Split {}
                TButton {
                    id: alignButton
                    symbol: "align-start-vertical"
                    active: (win.info.units ?? 0) > 1
                    onClicked: alignMenu.popup(alignButton, 0, alignButton.height + 4)
                }
                TButton { symbol: "group"; active: (win.info.units ?? 0) > 1; onClicked: win.run("group") }
                TButton { symbol: "ungroup"; active: win.info.grouped ?? false; onClicked: win.run("ungroup") }
                TButton { symbol: "bring-to-front"; active: win.some; onClicked: win.run("front") }
                TButton { symbol: "send-to-back"; active: win.some; onClicked: win.run("back") }
                Split {}
                TButton { symbol: "grid-3x3"; checked: ed.grid; onClicked: win.run("grid") }
                TButton { symbol: "magnet"; checked: ed.snap; onClicked: win.run("snap") }
            }
            Row {
                id: barRight
                x: parent.width - width - 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                spacing: 2
                Rectangle {
                    width: ordersRow.width + 18
                    height: 26
                    radius: 7
                    color: ordersHit.containsMouse ? Theme.sideSelection : Theme.field
                    Row {
                        id: ordersRow
                        x: 9
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Sym {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "search"
                            size: 13
                            color: Theme.dim
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Órdenes"
                            color: Theme.dim
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Ctrl K"
                            color: Theme.faint
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: ordersHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.run("palette")
                    }
                }
                Item { width: 6; height: 1 }
                TButton { symbol: "history"; onClicked: win.run("versions") }
                TButton { symbol: "share"; onClicked: win.run("export") }
                TButton { symbol: "panel-right"; checked: win.inspectorOpen; onClicked: win.run("inspector") }
            }
        }

        Sidebar {
            id: sidebar
            y: bar.height
            width: win.bars && win.sidebarShown ? 232 : 0
            height: parent.height - bar.height - status.height
            visible: width > 0
            clip: true
            onFrameMenu: anchor => frameMenu.popup(anchor, 0, anchor.height + 4)
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
            width: win.bars && win.inspectorOpen ? 264 : 0
            height: parent.height - bar.height - status.height
            visible: width > 0
            clip: true
            onRun: action => win.run(action)
        }

        // The canvas and what floats over it.
        Item {
            id: stage
            x: sidebar.width
            y: bar.height
            width: parent.width - sidebar.width - inspector.width
            height: parent.height - bar.height - status.height
            clip: true

            Board {
                id: board
                anchors.fill: parent
                editor: App.editor
                paper: Theme.canvas
                dots: Theme.canvasDots
                accent: Theme.tint
                chrome: Theme.dim
                focus: true
                onMenuRequested: (x, y) => contextMenu.popup(board, x, y)
                // The space bar held turns the pointer into a hand.
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                        ed.setSpacePan(true)
                        event.accepted = true
                    }
                }
                Keys.onReleased: event => {
                    if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
                        ed.setSpacePan(false)
                        event.accepted = true
                    }
                }
            }

            // Where the words of an object are typed, laid over it.
            Item {
                id: typer
                property string target: ""
                property var box: ({})
                function begin(id) {
                    if (target !== "")
                        finish()
                    const b = ed.textBox(id)
                    if (b.w === undefined)
                        return
                    box = b
                    target = id
                    ed.editing = id
                    words.text = b.text
                    words.forceActiveFocus()
                    words.selectAll()
                }
                function place() {
                    if (target !== "") {
                        const b = ed.textBox(target)
                        if (b.w !== undefined)
                            box = b
                    }
                }
                function finish() {
                    if (target === "")
                        return
                    const id = target
                    target = ""
                    ed.editing = ""
                    ed.setText(id, words.text)
                    board.forceActiveFocus()
                }
                visible: target !== ""
                width: Math.max(box.w ?? 10, box.autoSize ? words.implicitWidth + 2 * (box.pad ?? 0) + 4 : 0)
                height: Math.max(box.h ?? 10, box.autoSize ? words.implicitHeight + 2 * (box.pad ?? 0) : 0)
                x: (box.cx ?? 0) - (box.w ?? 0) / 2 - (box.hAlign === "center" ? (width - (box.w ?? 0)) / 2 : 0)
                y: (box.cy ?? 0) - (box.h ?? 0) / 2
                transformOrigin: Item.Center
                transform: [
                    Scale { origin.x: (typer.box.w ?? 0) / 2; origin.y: (typer.box.h ?? 0) / 2; xScale: typer.box.zoom ?? 1; yScale: typer.box.zoom ?? 1 },
                    Rotation { origin.x: (typer.box.w ?? 0) / 2; origin.y: (typer.box.h ?? 0) / 2; angle: typer.box.rotation ?? 0 }
                ]
                TextEdit {
                    id: words
                    anchors.fill: parent
                    anchors.margins: typer.box.pad ?? 0
                    color: typer.box.color ?? Theme.text
                    font.pixelSize: typer.box.fontSize ?? 16
                    font.family: (typer.box.fontFamily ?? "") !== "" ? typer.box.fontFamily : Qt.application.font.family
                    font.bold: typer.box.bold ?? false
                    font.italic: typer.box.italic ?? false
                    font.underline: typer.box.underline ?? false
                    font.hintingPreference: Font.PreferNoHinting
                    wrapMode: typer.box.autoSize ? TextEdit.NoWrap : TextEdit.Wrap
                    horizontalAlignment: typer.box.hAlign === "left" ? TextEdit.AlignLeft : (typer.box.hAlign === "right" ? TextEdit.AlignRight : TextEdit.AlignHCenter)
                    verticalAlignment: typer.box.vAlign === "top" ? TextEdit.AlignTop : (typer.box.vAlign === "bottom" ? TextEdit.AlignBottom : TextEdit.AlignVCenter)
                    selectByMouse: true
                    selectionColor: Theme.accent
                    selectedTextColor: "#ffffff"
                    Keys.onEscapePressed: typer.finish()
                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                            typer.finish()
                            event.accepted = true
                        }
                    }
                    onActiveFocusChanged: if (!activeFocus && !App.testing) typer.finish()
                }
            }

            // The likeliest things to do with the selection, right by it.
            Rectangle {
                id: pill
                readonly property rect around: ed.selectionRect
                visible: win.some && !ed.busy && typer.target === "" && ed.tool === "select" && around.width + around.height > 0
                width: pillRow.width + 12
                height: 36
                radius: 11
                color: Theme.floating
                border.color: Theme.floatingBorder
                x: Math.max(8, Math.min(parent.width - width - 8, Math.round(around.x + around.width / 2 - width / 2)))
                y: {
                    const above = Math.round(around.y - height - 44)
                    if (above >= 8)
                        return above
                    const below = Math.round(around.y + around.height + 18)
                    return below + height <= parent.height - 70 ? below : 8
                }
                Row {
                    id: pillRow
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    ColorWell {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: (win.info.hasFill ?? false) && win.info.fillKind !== "none"
                        width: 30
                        value: win.info.fill ?? "#ffffff"
                        onPicked: color => ed.setProp("fill", color)
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !(win.info.isImage ?? false)
                        width: 30
                        height: 24
                        radius: 6
                        color: Theme.field
                        // A ring, to tell the line colour from the fill.
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16
                            height: 16
                            radius: 8
                            color: "transparent"
                            border.width: 3
                            border.color: win.info.stroke ?? "#000000"
                        }
                        ColorWell {
                            id: strokeWell
                            anchors.fill: parent
                            opacity: 0
                            value: win.info.stroke ?? "#000000"
                            onPicked: color => ed.setProp("stroke", color)
                        }
                    }
                    Item { width: 5; height: 1 }
                    TButton { visible: win.info.hasText ?? false; symbol: "type"; onClicked: win.run("edit-text") }
                    TButton { symbol: "copy-plus"; onClicked: win.run("duplicate") }
                    TButton { symbol: "bring-to-front"; onClicked: win.run("front") }
                    TButton { symbol: "send-to-back"; onClicked: win.run("back") }
                    TButton { visible: (win.info.units ?? 0) > 1; symbol: "group"; onClicked: win.run("group") }
                    TButton { visible: win.info.grouped ?? false; symbol: "ungroup"; onClicked: win.run("ungroup") }
                    TButton { symbol: (win.info.locked ?? false) ? "lock" : "lock-open"; checked: win.info.locked ?? false; onClicked: win.run("lock") }
                    TButton { symbol: "trash-2"; onClicked: win.run("delete") }
                }
            }

            // The tools, at the bottom in the middle.
            Rectangle {
                id: tools
                readonly property var list: ["select", "hand", "", "rect", "ellipse", "polygon", "star", "", "line", "arrow", "connector", "", "pen", "pencil", "", "text", "note", "image", "frame"]
                visible: !win.concentrated
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                width: toolRow.width + 12
                height: 44
                // In a narrow canvas the bar shrinks rather than lose its ends.
                scale: Math.min(1, (stage.width - 16) / width)
                transformOrigin: Item.Bottom
                radius: 13
                color: Theme.floating
                border.color: Theme.floatingBorder
                // Presses on the bar are not presses on the canvas.
                MouseArea { anchors.fill: parent }
                Row {
                    id: toolRow
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Repeater {
                        model: tools.list
                        Item {
                            id: slot
                            required property string modelData
                            readonly property var action: win.actions.find(a => a.id === "tool-" + modelData || a.id === modelData) ?? null
                            readonly property bool on: ed.tool === modelData
                            width: modelData === "" ? 9 : 32
                            height: 32
                            Rectangle {
                                visible: slot.modelData === ""
                                anchors.centerIn: parent
                                width: 1
                                height: 18
                                color: Theme.floatingBorder
                            }
                            Rectangle {
                                visible: slot.modelData !== ""
                                anchors.fill: parent
                                radius: 8
                                color: slot.on ? Theme.tint : (toolHit.containsMouse ? Theme.hover : "transparent")
                                Sym {
                                    anchors.centerIn: parent
                                    name: slot.action ? slot.action.symbol : ""
                                    size: 17
                                    color: slot.on ? "#ffffff" : Theme.text
                                }
                                MouseArea {
                                    id: toolHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: win.run(slot.action.id)
                                }
                                ToolTip.visible: toolHit.containsMouse
                                ToolTip.delay: 500
                                ToolTip.text: slot.action ? slot.action.title + "  (" + slot.action.shortcut + ")" : ""
                            }
                        }
                    }
                }
            }

            // The whole drawing, small.
            Rectangle {
                // Beside the tools when there is room, above them when not.
                readonly property bool beside: stage.width > tools.width + 2 * (width + 28)
                visible: win.minimapOpen && !win.concentrated && ed.count > 0 && stage.width > 420 && stage.height > 330
                x: parent.width - width - 14
                y: parent.height - height - (beside ? 14 : 70)
                width: 168
                height: 112
                radius: 10
                color: Theme.floating
                border.color: Theme.floatingBorder
                Minimap {
                    anchors.fill: parent
                    anchors.margins: 5
                    editor: App.editor
                    accent: Theme.tint
                }
            }

            // Out of concentration, for who forgot the key.
            Rectangle {
                visible: win.concentrated
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                width: leaveText.implicitWidth + 24
                height: 28
                radius: 14
                color: Theme.floating
                border.color: Theme.floatingBorder
                opacity: leaveHit.containsMouse ? 1 : 0.55
                Text {
                    id: leaveText
                    anchors.centerIn: parent
                    text: "Esc para salir"
                    color: Theme.dim
                    font.pixelSize: 11
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
                anchors.bottomMargin: 72
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
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                Text {
                    text: ed.count === 1 ? "1 objeto" : ed.count + " objetos"
                    color: Theme.dim
                    font.pixelSize: 11
                }
                Text {
                    visible: win.some
                    text: win.some ? (ed.selection.length === 1 ? (win.info.name ?? "") : ed.selection.length + " seleccionados")
                        + "  ·  " + Math.round(win.info.w ?? 0) + " × " + Math.round(win.info.h ?? 0) : ""
                    color: Theme.dim
                    font.pixelSize: 11
                }
                Text {
                    text: (win.actions.find(a => a.id === "tool-" + ed.tool.replace("form:", "")) ?? {title: "Forma"}).title
                    color: ed.tool === "select" ? Theme.dim : Theme.tint
                    font.pixelSize: 11
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(ed.pointer.x) + ", " + Math.round(ed.pointer.y)
                    color: Theme.faint
                    font.pixelSize: 11
                    width: 92
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 10
                }
                TButton { width: 24; height: 20; size: 12; tone: Theme.dim; symbol: "minus"; onClicked: win.run("zoom-out") }
                Rectangle {
                    id: zoomBox
                    width: 50
                    height: 20
                    radius: 5
                    color: zoomHit.containsMouse ? Theme.hover : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(ed.zoom * 100) + " %"
                        color: Theme.text
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: zoomHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: zoomMenu.popup(zoomBox, 0, -zoomMenu.height - 4)
                    }
                }
                TButton { width: 24; height: 20; size: 12; tone: Theme.dim; symbol: "plus"; onClicked: win.run("zoom-in") }
                TButton { width: 24; height: 20; size: 12; tone: Theme.dim; symbol: "maximize"; onClicked: win.run("zoom-fit") }
            }
        }
    }

    // --- Menus ----------------------------------------------------------------------

    FMenu {
        id: frameMenu
        Repeater {
            model: App.framePresets()
            FMenuItem {
                required property var modelData
                text: modelData.name + "  ·  " + modelData.w + " × " + modelData.h
                onTriggered: ed.addFrame(modelData.key)
            }
        }
        FSeparator {}
        FMenuItem { text: "Dibujar un marco"; onTriggered: win.run("tool-frame") }
    }
    FMenu {
        id: alignMenu
        FMenuItem { text: "Alinear a la izquierda"; onTriggered: win.run("align-left") }
        FMenuItem { text: "Centrar en horizontal"; onTriggered: win.run("align-hcenter") }
        FMenuItem { text: "Alinear a la derecha"; onTriggered: win.run("align-right") }
        FSeparator {}
        FMenuItem { text: "Alinear arriba"; onTriggered: win.run("align-top") }
        FMenuItem { text: "Centrar en vertical"; onTriggered: win.run("align-vcenter") }
        FMenuItem { text: "Alinear abajo"; onTriggered: win.run("align-bottom") }
        FSeparator {}
        FMenuItem { text: "Repartir en horizontal"; enabled: (win.info.units ?? 0) > 2; onTriggered: win.run("distribute-h") }
        FMenuItem { text: "Repartir en vertical"; enabled: (win.info.units ?? 0) > 2; onTriggered: win.run("distribute-v") }
    }
    FMenu {
        id: zoomMenu
        FMenuItem { text: "Encajar todo"; onTriggered: win.run("zoom-fit") }
        FMenuItem { text: "Encajar la selección"; enabled: win.some; onTriggered: win.run("zoom-selection") }
        FSeparator {}
        FMenuItem { text: "50 %"; onTriggered: ed.setZoom(0.5) }
        FMenuItem { text: "100 %"; onTriggered: ed.setZoom(1) }
        FMenuItem { text: "200 %"; onTriggered: ed.setZoom(2) }
        FMenuItem { text: "400 %"; onTriggered: ed.setZoom(4) }
    }
    FMenu {
        id: contextMenu
        FMenuItem { text: "Cortar"; enabled: win.some; onTriggered: win.run("cut") }
        FMenuItem { text: "Copiar"; enabled: win.some; onTriggered: win.run("copy") }
        FMenuItem { text: "Pegar"; onTriggered: win.run("paste") }
        FMenuItem { text: "Duplicar"; enabled: win.some; onTriggered: win.run("duplicate") }
        FSeparator {}
        FMenuItem { text: "Traer al frente"; enabled: win.some; onTriggered: win.run("front") }
        FMenuItem { text: "Enviar al fondo"; enabled: win.some; onTriggered: win.run("back") }
        FMenuItem { text: "Agrupar"; enabled: (win.info.units ?? 0) > 1; onTriggered: win.run("group") }
        FMenuItem { text: "Desagrupar"; enabled: win.info.grouped ?? false; onTriggered: win.run("ungroup") }
        FMenuItem { text: (win.info.locked ?? false) ? "Desbloquear" : "Bloquear"; enabled: win.some; onTriggered: win.run("lock") }
        FSeparator {}
        FMenuItem { text: "Seleccionar todo"; onTriggered: win.run("select-all") }
        FMenuItem { text: "Eliminar"; enabled: win.some; onTriggered: win.run("delete") }
    }

    // --- Dialogs --------------------------------------------------------------------

    CommandPalette {
        id: palette
        host: win
    }
    ConfirmDialog {
        id: confirmDialog
    }
    ExportDialog {
        id: exportDialog
        onAccepted: (format, frame, scale, transparent) => {
            exportFile.format = format
            exportFile.frame = frame
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
        title: "Abrir un dibujo"
        nameFilters: ["Dibujos (*.wdib *.svg)", "Dibujo de Wonderland (*.wdib)", "SVG (*.svg)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            // One drawing per window: with one open, the other gets its own.
            if (App.open)
                App.openInNewWindow(App.localPath(selectedFile))
            else
                App.openUrl(selectedFile)
        }
    }
    FileDialog {
        id: saveDialog
        function ask() {
            selectedFile = App.suggestedUrl("wdib")
            open()
        }
        title: "Guardar el dibujo"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wdib"
        nameFilters: ["Dibujo de Wonderland (*.wdib)"]
        onAccepted: App.saveAs(selectedFile)
    }
    FileDialog {
        id: exportFile
        property string format: "png"
        property string frame
        property real scale: 2
        property bool transparent: false
        title: "Exportar"
        fileMode: FileDialog.SaveFile
        defaultSuffix: format
        onAccepted: App.exportTo(selectedFile, format, frame, scale, transparent)
    }
    FileDialog {
        id: importDialog
        title: "Importar un SVG"
        nameFilters: ["SVG (*.svg)"]
        onAccepted: App.importSvg(selectedFile)
    }
    FileDialog {
        id: imageDialog
        title: "Insertar una imagen"
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: App.insertImage(selectedFile)
    }
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            for (const url of drop.urls) {
                const path = App.localPath(url)
                if (!App.open)
                    App.openUrl(url)
                else if (/\.svg$/i.test(path))
                    App.importSvg(url)
                else if (/\.wdib$/i.test(path))
                    App.openInNewWindow(path)
                else
                    App.insertImage(url)
            }
        }
    }

    // --- Pictures for the tests (--shot, --do) --------------------------------------
    //
    // Every action of the registry is a verb; these are the ones that take a word.

    function point(arg) {
        const parts = arg.split(",").map(Number)
        return {x: parts[0], y: parts[1], mods: parts.length > 2 ? parts[2] : 0}
    }
    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        // A registry id may hold a colon of its own.
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
            // export:<format>:<path>[:<frame name>]
            const parts = arg.split(":")
            const frame = ed.frames.find(f => f.name === parts[2])
            App.exportTo("file://" + parts[1], parts[0], frame ? frame.id : "", 2, false)
            break
        }
        case "import":
            App.importSvg("file://" + arg)
            break
        case "picture":
            App.insertImage("file://" + arg)
            break
        case "welcome":
            App.closeDocument()
            break
        case "tool":
            ed.tool = arg
            break
        case "select": {
            // By name, several split by «,»; «all» and «none» too.
            if (arg === "all") {
                ed.selectAll()
                break
            }
            ed.clearSelection()
            for (const name of arg.split(",")) {
                const layer = ed.layers.find(l => l.name === name)
                if (layer)
                    ed.select(layer.id, true)
            }
            break
        }
        case "set": {
            const eq = arg.indexOf("=")
            const raw = arg.slice(eq + 1)
            ed.setProp(arg.slice(0, eq), raw === "true" ? true : (raw === "false" ? false : (raw !== "" && !isNaN(Number(raw)) ? Number(raw) : raw)))
            break
        }
        case "form":
            ed.addForm(arg, 0, 0)
            break
        case "text":
            ed.addText(arg)
            break
        case "note":
            ed.addNote(arg)
            break
        case "addframe":
            ed.addFrame(arg)
            break
        case "press": {
            const p = point(arg)
            ed.press(p.x, p.y, 1, p.mods)
            break
        }
        case "move": {
            const p = point(arg)
            ed.move(p.x, p.y, p.mods)
            break
        }
        case "release": {
            const p = point(arg)
            ed.release(p.x, p.y, p.mods)
            break
        }
        case "drag": {
            // drag:x1,y1,x2,y2 — a whole gesture, in steps as a hand would.
            const n = arg.split(",").map(Number)
            ed.press(n[0], n[1], 1, 0)
            for (let i = 1; i <= 8; ++i)
                ed.move(n[0] + (n[2] - n[0]) * i / 8, n[1] + (n[3] - n[1]) * i / 8, 0)
            ed.release(n[2], n[3], 0)
            break
        }
        case "click": {
            const p = point(arg)
            ed.press(p.x, p.y, 1, p.mods)
            ed.release(p.x, p.y, p.mods)
            break
        }
        case "double": {
            const p = point(arg)
            ed.doubleClick(p.x, p.y)
            break
        }
        case "type":
            words.text = arg
            break
        case "done":
            typer.finish()
            break
        case "zoom":
            ed.setZoom(Number(arg))
            break
        case "pan": {
            const p = point(arg)
            ed.panBy(p.x, p.y)
            break
        }
        case "reveal": {
            const layer = ed.layers.find(l => l.name === arg)
            if (layer)
                ed.reveal(layer.id)
            break
        }
        case "tab":
            sidebar.tab = arg
            break
        case "panels":
            sidebarOpen = arg === "on"
            inspectorOpen = arg === "on"
            break
        case "find":
            palette.show(arg)
            break
        case "menu":
            if (arg === "frame")
                frameMenu.popup(frameButton, 0, frameButton.height + 4)
            else if (arg === "align")
                alignMenu.popup(alignButton, 0, alignButton.height + 4)
            else if (arg === "zoom")
                zoomMenu.popup(zoomBox, 0, -260)
            else
                contextMenu.popup(board, 300, 200)
            break
        case "exportas":
            exportDialog.show(arg === "" ? "png" : arg)
            break
        case "color":
            strokeWell.open()
            break
        case "confirm":
            closeDrawing()
            break
        case "title":
            ed.title = arg
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
