import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Window

ApplicationWindow {
    id: win

    readonly property var deck: App.deck
    property bool sidebarOpen: App.setting("sidebar", true)
    property bool inspectorOpen: App.setting("inspector", true)
    /// slides or outline.
    property string sideTab: "slides"
    /// format, slide or theme.
    property string inspectorTab: "format"
    /// Pixels per point of the slide; 0 for whatever shows it whole.
    property real zoom: 0
    /// The theme being tried on by passing over it, if any.
    property string tryTheme: ""
    /// The grid of every slide instead of the one in hand.
    property bool sorting: false
    /// Nothing but the slide.
    property bool concentrated: false
    property bool presenting: false
    property bool renaming: false
    property int visibilityBefore: Window.Windowed
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool busy: palette.visible || versionsPanel.visible || confirmDialog.visible || leaveDialog.visible || keysPanel.visible
    readonly property var it: { deck.revision; return deck.selection.length > 0 ? deck.item(deck.selection[0]) : ({}) }
    readonly property var zoomSteps: [0.25, 0.33, 0.5, 0.67, 0.75, 1, 1.25, 1.5, 2, 3, 4]

    width: App.testing ? App.testSize.width : App.setting("width", 1240)
    height: App.testing ? App.testSize.height : App.setting("height", 780)
    minimumWidth: 900
    minimumHeight: 540
    visible: true
    title: App.started ? deck.name + " — Diapositivas" : "Diapositivas"
    color: App.glass ? "transparent" : Theme.window

    onClosing: close => {
        if (App.testing)
            return
        if (App.started && deck.modified && !leaveDialog.decided) {
            close.accepted = false
            leaveDialog.open()
            return
        }
        if (visibility === Window.Windowed) {
            App.setSetting("width", width)
            App.setSetting("height", height)
        }
        App.setSetting("sidebar", sidebarOpen)
        App.setSetting("inspector", inspectorOpen)
    }
    Component.onCompleted: if (App.testing) testStep.start()

    // --- The orders: the palette, the keys and the verbs of --do all come from here ---

    /// [{id, title, section, keys, symbol, quiet, hidden}]. `quiet`: the key is the text's while typing.
    readonly property var actions: [
        {id: "new", title: "Nueva presentación", section: "Archivo", keys: "Ctrl+N", symbol: "file-plus"},
        {id: "open", title: "Abrir…", section: "Archivo", keys: "Ctrl+O", symbol: "folder-open"},
        {id: "save", title: "Guardar", section: "Archivo", keys: "Ctrl+S", symbol: "save"},
        {id: "saveAs", title: "Guardar como…", section: "Archivo", keys: "Ctrl+Shift+S", symbol: "save"},
        {id: "rename", title: "Renombrar la presentación", section: "Archivo", keys: "F2", symbol: "pencil"},
        {id: "versions", title: "Versiones…", section: "Archivo", keys: "", symbol: "clock"},
        {id: "exportPdf", title: "Exportar como PDF…", section: "Archivo", keys: "Ctrl+Shift+E", symbol: "file-down"},
        {id: "exportPng", title: "Exportar una imagen por diapositiva…", section: "Archivo", keys: "", symbol: "file-image"},
        {id: "close", title: "Cerrar la ventana", section: "Archivo", keys: "Ctrl+W", symbol: "x"},

        {id: "undo", title: "Deshacer", section: "Editar", keys: "Ctrl+Z", symbol: "undo-2", quiet: true},
        {id: "redo", title: "Rehacer", section: "Editar", keys: "Ctrl+Shift+Z", symbol: "redo-2", quiet: true},
        {id: "cut", title: "Cortar", section: "Editar", keys: "Ctrl+X", symbol: "scissors", quiet: true},
        {id: "copy", title: "Copiar", section: "Editar", keys: "Ctrl+C", symbol: "copy", quiet: true},
        {id: "paste", title: "Pegar", section: "Editar", keys: "Ctrl+V", symbol: "clipboard", quiet: true},
        {id: "duplicate", title: "Duplicar", section: "Editar", keys: "Ctrl+D", symbol: "copy-plus"},
        {id: "delete", title: "Eliminar", section: "Editar", keys: "Del", symbol: "trash-2", quiet: true},
        {id: "selectAll", title: "Seleccionar todo", section: "Editar", keys: "Ctrl+A", symbol: "square-dashed", quiet: true},
        {id: "editText", title: "Escribir en lo seleccionado", section: "Editar", keys: "Return", symbol: "text-cursor-input", quiet: true},
        {id: "escape", title: "Soltar la selección", section: "Editar", keys: "Esc", symbol: "x"},

        {id: "addText", title: "Cuadro de texto", section: "Insertar", keys: "Ctrl+T", symbol: "type"},
        {id: "shape-rect", title: "Rectángulo", section: "Insertar", keys: "", symbol: "square"},
        {id: "shape-rounded", title: "Rectángulo redondeado", section: "Insertar", keys: "", symbol: "square-round-corner"},
        {id: "shape-ellipse", title: "Elipse", section: "Insertar", keys: "", symbol: "circle"},
        {id: "shape-triangle", title: "Triángulo", section: "Insertar", keys: "", symbol: "triangle"},
        {id: "shape-diamond", title: "Rombo", section: "Insertar", keys: "", symbol: "diamond"},
        {id: "shape-star", title: "Estrella", section: "Insertar", keys: "", symbol: "star"},
        {id: "shape-arrow", title: "Flecha de bloque", section: "Insertar", keys: "", symbol: "arrow-right"},
        {id: "shape-line", title: "Línea", section: "Insertar", keys: "", symbol: "minus"},
        {id: "shape-connector", title: "Flecha", section: "Insertar", keys: "", symbol: "move-up-right"},
        {id: "addImage", title: "Imagen…", section: "Insertar", keys: "Ctrl+Shift+I", symbol: "image-plus"},
        {id: "addTable", title: "Tabla", section: "Insertar", keys: "", symbol: "table"},

        {id: "slideNew", title: "Nueva diapositiva", section: "Diapositiva", keys: "Ctrl+M", symbol: "plus"},
        {id: "slideDuplicate", title: "Duplicar la diapositiva", section: "Diapositiva", keys: "Ctrl+Shift+D", symbol: "copy-plus"},
        {id: "slideDelete", title: "Eliminar la diapositiva", section: "Diapositiva", keys: "Ctrl+Backspace", symbol: "trash-2"},
        {id: "slideUp", title: "Subir la diapositiva", section: "Diapositiva", keys: "Ctrl+Shift+Up", symbol: "arrow-up"},
        {id: "slideDown", title: "Bajar la diapositiva", section: "Diapositiva", keys: "Ctrl+Shift+Down", symbol: "arrow-down"},
        {id: "slideHide", title: "Omitir al presentar", section: "Diapositiva", keys: "Ctrl+Shift+H", symbol: "eye-off"},
        {id: "slidePrev", title: "Diapositiva anterior", section: "Diapositiva", keys: "PgUp", symbol: "chevron-up"},
        {id: "slideNext", title: "Diapositiva siguiente", section: "Diapositiva", keys: "PgDown", symbol: "chevron-down"},
        {id: "layout-title", title: "Diseño: Título", section: "Diapositiva", keys: "", symbol: "layout-template"},
        {id: "layout-content", title: "Diseño: Título y contenido", section: "Diapositiva", keys: "", symbol: "layout-template"},
        {id: "layout-columns", title: "Diseño: Dos columnas", section: "Diapositiva", keys: "", symbol: "columns-2"},
        {id: "layout-titleOnly", title: "Diseño: Solo título", section: "Diapositiva", keys: "", symbol: "layout-panel-top"},
        {id: "layout-blank", title: "Diseño: En blanco", section: "Diapositiva", keys: "", symbol: "square"},
        {id: "layout-picture", title: "Diseño: Imagen con pie", section: "Diapositiva", keys: "", symbol: "image"},
        {id: "transition-none", title: "Transición: ninguna", section: "Diapositiva", keys: "", symbol: "sparkles"},
        {id: "transition-dissolve", title: "Transición: disolver", section: "Diapositiva", keys: "", symbol: "sparkles"},
        {id: "transition-push", title: "Transición: empujar", section: "Diapositiva", keys: "", symbol: "sparkles"},
        {id: "transition-fade", title: "Transición: fundido a negro", section: "Diapositiva", keys: "", symbol: "sparkles"},

        {id: "bold", title: "Negrita", section: "Formato", keys: "Ctrl+B", symbol: "bold"},
        {id: "italic", title: "Cursiva", section: "Formato", keys: "Ctrl+I", symbol: "italic"},
        {id: "underline", title: "Subrayado", section: "Formato", keys: "Ctrl+U", symbol: "underline"},
        {id: "strike", title: "Tachado", section: "Formato", keys: "Ctrl+Shift+X", symbol: "strikethrough"},
        {id: "bigger", title: "Texto más grande", section: "Formato", keys: "Ctrl++", symbol: "a-arrow-up"},
        {id: "smaller", title: "Texto más pequeño", section: "Formato", keys: "Ctrl+-", symbol: "a-arrow-down"},
        {id: "bullets", title: "Viñetas", section: "Formato", keys: "Ctrl+Shift+L", symbol: "list"},
        {id: "textLeft", title: "Texto a la izquierda", section: "Formato", keys: "", symbol: "text-align-start"},
        {id: "textCenter", title: "Texto centrado", section: "Formato", keys: "", symbol: "text-align-center"},
        {id: "textRight", title: "Texto a la derecha", section: "Formato", keys: "", symbol: "text-align-end"},
        {id: "shadow", title: "Sombra", section: "Formato", keys: "", symbol: "layers"},
        {id: "lineArrow", title: "Punta de flecha", section: "Formato", keys: "", symbol: "move-right"},
        {id: "animNone", title: "Animación de entrada: ninguna", section: "Formato", keys: "", symbol: "zap"},
        {id: "animAppear", title: "Animación de entrada: aparecer", section: "Formato", keys: "", symbol: "zap"},
        {id: "animSlide", title: "Animación de entrada: deslizar", section: "Formato", keys: "", symbol: "zap"},

        {id: "alignLeft", title: "Alinear a la izquierda", section: "Organizar", keys: "", symbol: "align-start-vertical"},
        {id: "alignCenter", title: "Centrar en horizontal", section: "Organizar", keys: "", symbol: "align-center-vertical"},
        {id: "alignRight", title: "Alinear a la derecha", section: "Organizar", keys: "", symbol: "align-end-vertical"},
        {id: "alignTop", title: "Alinear arriba", section: "Organizar", keys: "", symbol: "align-start-horizontal"},
        {id: "alignMiddle", title: "Centrar en vertical", section: "Organizar", keys: "", symbol: "align-center-horizontal"},
        {id: "alignBottom", title: "Alinear abajo", section: "Organizar", keys: "", symbol: "align-end-horizontal"},
        {id: "distributeH", title: "Distribuir en horizontal", section: "Organizar", keys: "", symbol: "align-horizontal-distribute-center"},
        {id: "distributeV", title: "Distribuir en vertical", section: "Organizar", keys: "", symbol: "align-vertical-distribute-center"},
        {id: "front", title: "Traer al frente", section: "Organizar", keys: "Ctrl+Shift+]", symbol: "bring-to-front"},
        {id: "forward", title: "Traer adelante", section: "Organizar", keys: "Ctrl+]", symbol: "chevrons-up"},
        {id: "backward", title: "Enviar atrás", section: "Organizar", keys: "Ctrl+[", symbol: "chevrons-down"},
        {id: "back", title: "Enviar al fondo", section: "Organizar", keys: "Ctrl+Shift+[", symbol: "send-to-back"},
        {id: "group", title: "Agrupar", section: "Organizar", keys: "Ctrl+G", symbol: "group"},
        {id: "ungroup", title: "Desagrupar", section: "Organizar", keys: "Ctrl+Shift+G", symbol: "ungroup"},
        {id: "flipH", title: "Voltear en horizontal", section: "Organizar", keys: "", symbol: "flip-horizontal-2"},
        {id: "flipV", title: "Voltear en vertical", section: "Organizar", keys: "", symbol: "flip-vertical-2"},
        {id: "nudgeLeft", title: "Mover a la izquierda", section: "Organizar", keys: "Left", symbol: "chevron-left", quiet: true},
        {id: "nudgeRight", title: "Mover a la derecha", section: "Organizar", keys: "Right", symbol: "chevron-right", quiet: true},
        {id: "nudgeUp", title: "Mover arriba", section: "Organizar", keys: "Up", symbol: "chevron-up", quiet: true},
        {id: "nudgeDown", title: "Mover abajo", section: "Organizar", keys: "Down", symbol: "chevron-down", quiet: true},

        {id: "commands", title: "Órdenes…", section: "Ver", keys: "Ctrl+K", symbol: "search", hidden: true},
        {id: "sidebar", title: "Barra lateral", section: "Ver", keys: "Ctrl+Alt+S", symbol: "panel-left"},
        {id: "inspector", title: "Inspector", section: "Ver", keys: "Ctrl+Alt+I", symbol: "panel-right"},
        {id: "showSlides", title: "Miniaturas de las diapositivas", section: "Ver", keys: "Ctrl+1", symbol: "rows-3"},
        {id: "showOutline", title: "Esquema a diapositivas", section: "Ver", keys: "Ctrl+2", symbol: "list"},
        {id: "sorter", title: "Clasificador de diapositivas", section: "Ver", keys: "Ctrl+3", symbol: "layout-grid"},
        {id: "themes", title: "Temas", section: "Ver", keys: "Ctrl+Shift+T", symbol: "palette"},
        {id: "focus", title: "Modo concentración", section: "Ver", keys: "Ctrl+Shift+F", symbol: "maximize-2"},
        {id: "zoomIn", title: "Acercar", section: "Ver", keys: "Ctrl+.", symbol: "zoom-in"},
        {id: "zoomOut", title: "Alejar", section: "Ver", keys: "Ctrl+,", symbol: "zoom-out"},
        {id: "zoomFit", title: "Ajustar a la ventana", section: "Ver", keys: "Ctrl+0", symbol: "maximize"},
        {id: "zoom100", title: "Tamaño real", section: "Ver", keys: "", symbol: "search"},
        {id: "keys", title: "Atajos de teclado", section: "Ver", keys: "Ctrl+/", symbol: "keyboard"},

        {id: "present", title: "Presentar desde el principio", section: "Presentar", keys: "F5", symbol: "play"},
        {id: "presentHere", title: "Presentar desde esta diapositiva", section: "Presentar", keys: "Shift+F5", symbol: "skip-forward"},
        {id: "presenter", title: "Vista del presentador", section: "Presentar", keys: "Alt+F5", symbol: "monitor-play"},

        // Only for --do: what a hand would do with the mouse or the keyboard.
        {id: "sample", hidden: true}, {id: "welcome", hidden: true}, {id: "start", hidden: true}, {id: "slide", hidden: true},
        {id: "select", hidden: true}, {id: "tab", hidden: true}, {id: "side", hidden: true}, {id: "theme", hidden: true},
        {id: "try", hidden: true}, {id: "outline", hidden: true}, {id: "text", hidden: true}, {id: "notes", hidden: true},
        {id: "zoom", hidden: true}, {id: "next", hidden: true}, {id: "prev", hidden: true}, {id: "aspect", hidden: true},
        {id: "menu", hidden: true}
    ]
    function action(id) {
        return actions.find(entry => entry.id === id) || null
    }
    /// Whether an order that switches something has it on.
    function isOn(id) {
        const words = stage.wordsFormat()
        switch (id) {
        case "bold": return words ? words.bold : it.bold === true
        case "italic": return words ? words.italic : it.italic === true
        case "underline": return words ? words.underline : it.underline === true
        case "strike": return words ? words.strikeout : it.strike === true
        case "bullets": return it.bullet === true
        case "textLeft": return it.align === 0
        case "textCenter": return it.align === 1
        case "textRight": return it.align === 2
        case "lineArrow": return it.arrow === true
        case "shadow": return it.shadow === true
        case "slideHide": return deck.slide(deck.current).hidden === true
        case "sidebar": return sidebarOpen
        case "inspector": return inspectorOpen
        case "sorter": return sorting
        case "focus": return concentrated
        }
        return false
    }
    /// Formats the words selected in the text being typed, or all the text of what is selected.
    function format(key, value) {
        if (stage.editing !== "") {
            if (stage.formatWords(key, value))
                return
            stage.commit()
        }
        deck.setFormat(key, value)
    }
    function formatWords(key, value) {
        return stage.formatWords(key, value)
    }
    function resize(direction) {
        const words = stage.wordsFormat()
        const size = words ? words.pixelSize : (it.size || 24)
        const step = size >= 48 ? 6 : size >= 24 ? 4 : 2
        format("size", Math.max(6, Math.min(400, size + direction * step)))
    }
    function setZoom(direction) {
        const now = zoom > 0 ? zoom : stage.fit
        let next = now
        if (direction > 0)
            next = zoomSteps.find(step => step > now + 0.005) || zoomSteps[zoomSteps.length - 1]
        else
            next = zoomSteps.slice().reverse().find(step => step < now - 0.005) || zoomSteps[0]
        zoom = next
    }
    function added(id) {
        sorting = false
        if (id !== "")
            inspectorTab = "format"
    }
    function saveNow() {
        stage.commit()
        if (deck.path === "")
            saveDialog.open()
        else
            App.save(deck.path)
    }
    function startShow(fromHere, withSpeaker) {
        stage.commit()
        if (!show.begin(fromHere, withSpeaker)) {
            toast.say("Todas las diapositivas están omitidas: no hay nada que presentar.")
            return
        }
        presenting = true
        if (!App.testing) {
            visibilityBefore = visibility
            showFullScreen()
        }
        show.forceActiveFocus()
    }
    function endShow() {
        presenting = false
        if (!App.testing)
            visibility = visibilityBefore === Window.FullScreen ? Window.Windowed : visibilityBefore
        stage.forceActiveFocus()
    }

    function run(id, arg) {
        if (!App.started && ["new", "open", "close", "commands", "keys", "sample", "start", "welcome"].indexOf(id) < 0)
            return
        const value = arg === undefined ? "" : String(arg)
        if (id.indexOf("shape-") === 0) {
            stage.commit()
            added(deck.addShape(id.slice(6)))
            return
        }
        if (id.indexOf("layout-") === 0) {
            deck.setSlide(deck.current, "layout", id.slice(7))
            return
        }
        if (id.indexOf("transition-") === 0) {
            deck.setSlide(deck.current, "transition", id.slice(11) === "none" ? "" : id.slice(11))
            return
        }
        switch (id) {
        // Archivo
        case "new": App.started ? App.launch() : App.start("blanco", "16:9"); break
        case "open": value !== "" ? openFile(value) : openDialog.open(); break
        case "save": value !== "" ? App.save(value) : saveNow(); break
        case "saveAs": stage.commit(); value !== "" ? App.save(value) : saveDialog.open(); break
        case "rename":
            if (value !== "") {
                App.rename(value)
            } else {
                concentrated = false
                renaming = true
                nameField.text = deck.name
                nameField.forceActiveFocus()
                nameField.selectAll()
            }
            break
        case "versions": versionsPanel.show(); break
        case "exportPdf": stage.commit(); value !== "" ? App.exportPdf(value) : pdfDialog.open(); break
        case "exportPng": stage.commit(); value !== "" ? App.exportPng(value) : pngDialog.open(); break
        case "close": win.close(); break
        // Editar
        case "undo": stage.commit(); deck.undo(); break
        case "redo": stage.commit(); deck.redo(); break
        case "cut": deck.cut(); break
        case "copy": deck.copy(); break
        case "paste": deck.paste(); break
        case "duplicate": deck.duplicate(); break
        case "delete": stage.commit(); deck.remove(); break
        case "selectAll": deck.selectAll(); break
        case "editText": if (deck.selection.length === 1) stage.begin(deck.selection[0]); break
        case "escape":
            if (stage.editing !== "")
                stage.commit()
            else if (renaming)
                renaming = false
            else if (deck.selection.length > 0)
                deck.clearSelection()
            else if (concentrated)
                concentrated = false
            else if (sorting)
                sorting = false
            break
        // Insertar
        case "addText": stage.commit(); added(deck.addText()); break
        case "addImage":
            stage.commit()
            if (value !== "")
                added(deck.addImage(value))
            else
                imageDialog.open()
            break
        case "addTable": stage.commit(); added(deck.addTable(3, 3)); break
        // Diapositiva
        case "slideNew": stage.commit(); deck.addSlide(deck.current === 0 && deck.count === 1 ? "content" : (deck.slide(deck.current).layout === "title" ? "content" : deck.slide(deck.current).layout), deck.current + 1); break
        case "slideDuplicate": stage.commit(); deck.duplicateSlide(deck.current); break
        case "slideDelete": stage.commit(); if (deck.count > 1) deck.removeSlide(deck.current); break
        case "slideUp": if (deck.current > 0) deck.moveSlide(deck.current, deck.current - 1); break
        case "slideDown": if (deck.current < deck.count - 1) deck.moveSlide(deck.current, deck.current + 1); break
        case "slideHide": deck.setSlide(deck.current, "hidden", !(deck.slide(deck.current).hidden === true)); break
        case "slidePrev": stage.commit(); deck.current = Math.max(0, deck.current - 1); break
        case "slideNext": stage.commit(); deck.current = Math.min(deck.count - 1, deck.current + 1); break
        // Formato
        case "bold": format("bold", !isOn("bold")); break
        case "italic": format("italic", !isOn("italic")); break
        case "underline": format("underline", !isOn("underline")); break
        case "strike": format("strike", !isOn("strike")); break
        case "bigger": resize(1); break
        case "smaller": resize(-1); break
        case "bullets": format("bullet", !isOn("bullets")); break
        case "textLeft": format("align", 0); break
        case "textCenter": format("align", isOn("textCenter") ? 0 : 1); break
        case "textRight": format("align", 2); break
        case "shadow": deck.setProp("shadow", !isOn("shadow")); break
        case "lineArrow": deck.setProp("arrow", !isOn("lineArrow")); break
        case "animNone": deck.setProp("anim", ""); break
        case "animAppear": deck.setProp("anim", "appear"); break
        case "animSlide": deck.setProp("anim", "slide"); break
        // Organizar
        case "alignLeft": deck.align("left"); break
        case "alignCenter": deck.align("center"); break
        case "alignRight": deck.align("right"); break
        case "alignTop": deck.align("top"); break
        case "alignMiddle": deck.align("middle"); break
        case "alignBottom": deck.align("bottom"); break
        case "distributeH": deck.distribute("h"); break
        case "distributeV": deck.distribute("v"); break
        case "front": deck.order("front"); break
        case "forward": deck.order("forward"); break
        case "backward": deck.order("backward"); break
        case "back": deck.order("back"); break
        case "group": deck.group(); break
        case "ungroup": deck.ungroup(); break
        case "flipH": deck.setProp("flipH", !(it.flipH === true)); break
        case "flipV": deck.setProp("flipV", !(it.flipV === true)); break
        case "nudgeLeft": deck.selection.length > 0 ? deck.nudge(-1, 0) : run("slidePrev"); break
        case "nudgeRight": deck.selection.length > 0 ? deck.nudge(1, 0) : run("slideNext"); break
        case "nudgeUp": deck.selection.length > 0 ? deck.nudge(0, -1) : run("slidePrev"); break
        case "nudgeDown": deck.selection.length > 0 ? deck.nudge(0, 1) : run("slideNext"); break
        // Ver
        case "commands": palette.show(value); break
        case "sidebar": concentrated = false; sidebarOpen = !sidebarOpen; break
        case "inspector": concentrated = false; inspectorOpen = !inspectorOpen; break
        case "showSlides": concentrated = false; sidebarOpen = true; sideTab = "slides"; break
        case "showOutline": concentrated = false; sidebarOpen = true; sideTab = "outline"; break
        case "sorter": stage.commit(); concentrated = false; sorting = !sorting; break
        case "themes": concentrated = false; inspectorOpen = true; inspectorTab = "theme"; break
        case "focus": concentrated = !concentrated; if (concentrated) { sorting = false; toast.say("Modo concentración. Esc vuelve.") } break
        case "zoomIn": setZoom(1); break
        case "zoomOut": setZoom(-1); break
        case "zoomFit": zoom = 0; break
        case "zoom100": zoom = 1; break
        case "keys": keysPanel.open(); break
        // Presentar
        case "present": startShow(false, false); break
        case "presentHere": startShow(true, false); break
        case "presenter": startShow(true, true); break
        // --do only
        case "sample": sample(); break
        case "welcome": break
        case "start": App.start(value === "" ? "blanco" : value, "16:9"); break
        case "aspect": deck.setAspect(value); break
        case "slide": deck.current = Math.max(0, Math.min(deck.count - 1, parseInt(value) - 1)); break
        case "select": {
            const all = deck.items(deck.current)
            if (value === "")
                deck.clearSelection()
            else
                deck.selectOnly(value.split(",").map(n => (all[parseInt(n) - 1] || {}).id).filter(found => found))
            break
        }
        case "tab": inspectorOpen = true; inspectorTab = value; break
        case "side": sidebarOpen = true; sideTab = value; break
        case "theme": deck.setTheme(value); break
        case "try": tryTheme = value; break
        case "outline": sidebar.writeOutline(value.replace(/\\n/g, "\n")); break
        case "text": if (deck.selection.length === 1) deck.setPlain(deck.selection[0], value.replace(/\\n/g, "\n")); break
        case "notes": deck.setSlide(deck.current, "notes", value); break
        case "zoom": zoom = parseFloat(value) / 100; break
        case "next": show.next(); break
        case "prev": show.back(); break
        case "menu": value === "shapes" ? shapesMenu.popup(shapesButton, 0, 30) : exportMenu.popup(exportButton, 0, 30); break
        }
    }
    function runAction(text) {
        const at = text.indexOf(":")
        const id = at < 0 ? text : text.slice(0, at)
        if (!action(id) && id.indexOf("shape-") !== 0) {
            console.warn("unknown action", id)
            return
        }
        run(id, at < 0 ? undefined : text.slice(at + 1))
    }
    function openFile(path) {
        // One window, one presentation.
        if (App.started && (deck.modified || deck.path !== "" || deck.count > 1))
            App.launch(path)
        else
            App.openPath(path)
    }

    /// A deck worth looking at, made through the same calls the window makes.
    function sample() {
        App.start("blanco", "16:9")
        deck.setOutline("# Wonder OS\nUn escritorio con estilo propio\n> Saludar y presentar al equipo.\n---\n"
            + "# Qué vamos a ver\n- El escritorio WonderlandWM\n- Las aplicaciones de la suite\n  - Textos, Hojas y Diapositivas\n  - Dibujo, Bases y Fórmulas\n- Lo que viene después\n> Tres minutos por punto; dejar tiempo para las preguntas.\n---\n"
            + "# Las cifras\n- Seis aplicaciones nuevas\n- Un solo estilo\n---\n"
            + "# Formas y líneas\n---\n"
            + "# Gracias\n- ¿Preguntas?")
        deck.current = 2
        const table = deck.addTable(4, 3)
        deck.setProp("x", 500); deck.setProp("y", 170); deck.setProp("w", 400); deck.setProp("h", 200)
        const cells = [["Trimestre", "Usuarios", "Apps"], ["T1", "1.200", "12"], ["T2", "2.850", "19"], ["T3", "4.100", "25"]]
        for (let r = 0; r < 4; ++r)
            for (let c = 0; c < 3; ++c)
                deck.setCell(table, r, c, cells[r][c])
        const body = deck.items(2).find(item => item.role === "body")
        if (body) {
            deck.selectOnly([body.id])
            deck.setProp("w", 420)
        }
        deck.setSlide(2, "transition", "push")
        deck.current = 3
        deck.setSlide(3, "layout", "titleOnly")
        const shapes = [["rounded", 90, 190, "@accent1"], ["ellipse", 370, 190, "@accent2"], ["star", 590, 190, "@accent3"]]
        for (const [kind, x, y, fill] of shapes) {
            deck.addShape(kind)
            deck.setProp("x", x); deck.setProp("y", y); deck.setProp("fill", fill)
        }
        deck.addShape("connector")
        deck.setProp("x", 110); deck.setProp("y", 420); deck.setProp("w", 620)
        const picture = App.samplePicture()
        if (picture !== "") {
            deck.addImage(picture)
            deck.setProp("x", 760); deck.setProp("y", 180); deck.setProp("w", 150); deck.setProp("h", 112); deck.setProp("shadow", true)
        }
        deck.setSlide(3, "transition", "dissolve")
        deck.setSlide(1, "transition", "fade")
        deck.clearSelection()
        deck.current = 1
        sorting = false
    }

    Connections {
        target: App
        function onSaid(message) { toast.say(message) }
        function onStartedChanged() { win.sorting = false; win.zoom = 0 }
    }

    Instantiator {
        model: win.actions.filter(entry => (entry.keys || "") !== "")
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.id === "delete" ? ["Del", "Backspace"] : modelData.id === "zoomIn" ? ["Ctrl+.", "Ctrl+="] : modelData.id === "redo" ? ["Ctrl+Shift+Z", "Ctrl+Y"] : [modelData.keys]
            context: Qt.WindowShortcut
            enabled: !win.busy && !win.presenting && !(modelData.quiet && win.typing) && !(win.renaming && modelData.id !== "escape")
                && (modelData.id !== "editText" || (win.deck.selection.length === 1 && !win.sorting))
            onActivated: win.run(modelData.id)
        }
    }

    // --- Files ------------------------------------------------------------------------

    FileDialog {
        id: openDialog
        title: "Abrir presentación"
        nameFilters: ["Presentaciones (*.pptx *.odp)", "Todos los archivos (*)"]
        currentFolder: App.folderOf(win.deck.path)
        onAccepted: win.openFile(App.localPath(selectedFile))
    }
    FileDialog {
        id: saveDialog
        property var then: null
        title: "Guardar presentación"
        fileMode: FileDialog.SaveFile
        readonly property bool asOdp: /\.odp$/i.test(win.deck.path)
        defaultSuffix: selectedNameFilter.index === (asOdp ? 0 : 1) ? "odp" : "pptx"
        nameFilters: asOdp ? ["OpenDocument (*.odp)", "PowerPoint (*.pptx)"] : ["PowerPoint (*.pptx)", "OpenDocument (*.odp)"]
        currentFolder: App.folderOf(win.deck.path)
        currentFile: App.fileUrl(App.localPath(currentFolder) + "/" + win.deck.name + (asOdp ? ".odp" : ".pptx"))
        onAccepted: {
            const done = App.save(App.localPath(selectedFile))
            const next = then
            then = null
            if (done && next)
                next()
        }
        onRejected: then = null
    }
    FileDialog {
        id: pdfDialog
        title: "Exportar como PDF"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF (*.pdf)"]
        currentFolder: App.folderOf(win.deck.path)
        currentFile: App.fileUrl(App.localPath(currentFolder) + "/" + win.deck.name + ".pdf")
        onAccepted: App.exportPdf(App.localPath(selectedFile))
    }
    FileDialog {
        id: pngDialog
        title: "Exportar una imagen por diapositiva"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["Imágenes PNG (*.png)"]
        currentFolder: App.folderOf(win.deck.path)
        currentFile: App.fileUrl(App.localPath(currentFolder) + "/" + win.deck.name + ".png")
        onAccepted: App.exportPng(App.localPath(selectedFile))
    }
    FileDialog {
        id: imageDialog
        title: "Insertar imagen"
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: {
            const id = win.deck.addImage(App.localPath(selectedFile))
            if (id === "")
                toast.say("Esa imagen no se puede leer.")
            win.added(id)
        }
    }

    // --- The window ---------------------------------------------------------------------

    Welcome {
        anchors.fill: parent
        visible: !App.started
        onChosen: (theme, aspect) => App.start(theme, aspect)
        onOpened: path => App.openPath(path)
        onBrowse: openDialog.open()
    }

    Item {
        id: editor
        anchors.fill: parent
        visible: App.started && !win.presenting

        // The one bar of the window.
        Rectangle {
            id: bar
            visible: !win.concentrated
            width: parent.width
            height: visible ? 46 : 0
            color: Theme.chrome
            z: 3
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            Row {
                id: leading
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                TButton {
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "panel-left"
                    checked: win.sidebarOpen
                    onClicked: win.run("sidebar")
                }
                Item {
                    id: name
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property real room: Math.max(90, tools.x - leading.x - 56)
                    width: win.renaming ? Math.min(room, 220) : Math.min(room, nameText.implicitWidth + (dot.visible ? 30 : 16))
                    height: 28
                    Rectangle {
                        anchors.fill: parent
                        visible: !win.renaming
                        radius: 7
                        color: nameArea.containsMouse ? Theme.hover : "transparent"
                    }
                    Text {
                        id: nameText
                        visible: !win.renaming
                        x: 8
                        width: parent.width - (dot.visible ? 30 : 16)
                        anchors.verticalCenter: parent.verticalCenter
                        text: win.deck.name
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    // Changes not saved yet.
                    Rectangle {
                        id: dot
                        visible: win.deck.modified && !win.renaming
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.tint
                    }
                    MouseArea {
                        id: nameArea
                        anchors.fill: parent
                        enabled: !win.renaming
                        hoverEnabled: true
                        onClicked: win.run("rename")
                    }
                    Field {
                        id: nameField
                        visible: win.renaming
                        anchors.fill: parent
                        font.weight: Font.DemiBold
                        onAccepted: {
                            if (App.rename(text))
                                win.renaming = false
                            else
                                wrong = true
                        }
                        onTextEdited: wrong = false
                        onActiveFocusChanged: if (!activeFocus) win.renaming = false
                        Keys.onEscapePressed: win.renaming = false
                    }
                }
            }

            Row {
                id: tools
                x: Math.max(leading.x + 150, Math.min(Math.round((parent.width - width) / 2), trailing.x - width - 12))
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                TButton { symbol: "undo-2"; active: win.deck.canUndo; onClicked: win.run("undo") }
                TButton { symbol: "redo-2"; active: win.deck.canRedo; onClicked: win.run("redo") }
                BarLine {}
                TButton { symbol: "plus"; onClicked: win.run("slideNew") }
                BarLine {}
                TButton { symbol: "type"; onClicked: win.run("addText") }
                TButton { id: shapesButton; symbol: "shapes"; onClicked: shapesMenu.popup(shapesButton, 0, 30) }
                TButton { symbol: "move-up-right"; onClicked: win.run("shape-connector") }
                TButton { symbol: "image-plus"; onClicked: win.run("addImage") }
                TButton { symbol: "table"; onClicked: win.run("addTable") }
                BarLine {}
                TButton { symbol: "layout-grid"; checked: win.sorting; onClicked: win.run("sorter") }
                TButton { symbol: "palette"; checked: win.inspectorOpen && win.inspectorTab === "theme"; onClicked: win.run("themes") }
                BarLine {}
                TButton { symbol: "play"; tone: Theme.tint; onClicked: win.run("presentHere") }
                TButton { symbol: "monitor-play"; onClicked: win.run("presenter") }
            }

            Row {
                id: trailing
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: bar.width >= 1080 ? 132 : 30
                    height: 26
                    radius: 6
                    color: bar.width >= 1080 ? Theme.field : (findArea.containsMouse ? Theme.hover : "transparent")
                    Sym { x: 7; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 15; color: bar.width >= 1080 ? Theme.dim : Theme.text }
                    Text {
                        visible: bar.width >= 1080
                        x: 29
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Órdenes"
                        color: Theme.dim
                        font.pixelSize: 13
                    }
                    Text {
                        visible: bar.width >= 1080
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Ctrl K"
                        color: Theme.faint
                        font.pixelSize: 11
                    }
                    MouseArea { id: findArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.run("commands") }
                }
                Item { width: 4; height: 1 }
                TButton { id: exportButton; symbol: "upload"; onClicked: exportMenu.popup(exportButton, exportButton.width - 230, 30) }
                TButton { symbol: "panel-right"; checked: win.inspectorOpen; onClicked: win.run("inspector") }
            }
        }

        Sidebar {
            id: sidebar
            ui: win
            visible: win.sidebarOpen && !win.concentrated
            y: bar.height
            width: visible ? 216 : 0
            height: parent.height - bar.height - status.height
        }
        Inspector {
            id: inspector
            ui: win
            visible: win.inspectorOpen && !win.concentrated
            anchors.right: parent.right
            y: bar.height
            width: visible ? 264 : 0
            height: parent.height - bar.height - status.height
        }
        Stage {
            id: stage
            ui: win
            visible: !win.sorting
            x: sidebar.width
            y: bar.height
            width: parent.width - sidebar.width - inspector.width
            height: parent.height - bar.height - status.height
        }
        Sorter {
            ui: win
            visible: win.sorting
            x: sidebar.width
            y: bar.height
            width: stage.width
            height: stage.height
        }

        // The live facts, and the zoom.
        Rectangle {
            id: status
            visible: !win.concentrated
            anchors.bottom: parent.bottom
            width: parent.width
            height: visible ? 26 : 0
            color: Theme.chrome
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Text {
                x: 12
                width: zoomRow.x - 24
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.dim
                font.pixelSize: 12
                elide: Text.ElideRight
                text: {
                    win.deck.revision
                    const parts = ["Diapositiva " + (win.deck.current + 1) + " de " + win.deck.count]
                    const chosen = win.deck.selection.length
                    if (chosen > 1) {
                        parts.push(chosen + " objetos seleccionados")
                    } else if (chosen === 1) {
                        const kind = win.it.type === "image" ? "Imagen" : win.it.type === "table" ? "Tabla" : win.it.line ? "Línea" : win.it.type === "text" ? "Texto" : "Forma"
                        parts.push(kind + " · " + Math.round(win.it.w) + " × " + Math.round(win.it.h) + " pt")
                    }
                    const theme = win.deck.themes().find(entry => entry.id === (win.tryTheme || win.deck.theme))
                    if (theme)
                        parts.push((win.tryTheme !== "" ? "Probando el tema " : "Tema ") + theme.name)
                    parts.push(win.deck.aspect)
                    if (win.deck.slide(win.deck.current).hidden === true)
                        parts.push("Omitida al presentar")
                    return parts.join("   ·   ")
                }
            }
            Row {
                id: zoomRow
                visible: !win.sorting
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                TButton { width: 24; height: 20; size: 13; symbol: "minus"; onClicked: win.run("zoomOut") }
                Rectangle {
                    width: 54
                    height: 20
                    radius: 5
                    color: fitArea.containsMouse ? Theme.hover : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(stage.s * 100) + " %"
                        color: win.zoom > 0 ? Theme.text : Theme.dim
                        font.pixelSize: 12
                    }
                    MouseArea { id: fitArea; anchors.fill: parent; hoverEnabled: true; onClicked: win.run("zoomFit") }
                }
                TButton { width: 24; height: 20; size: 13; symbol: "plus"; onClicked: win.run("zoomIn") }
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
            z: 5
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: status.height + 18
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
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                color: "#ffffff"
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Timer {
                id: toastTimer
                interval: 3200
                onTriggered: toast.opacity = 0
            }
        }
    }

    Show {
        id: show
        ui: win
        anchors.fill: parent
        visible: win.presenting
    }

    FMenu {
        id: shapesMenu
        FMenuItem { text: "Rectángulo"; onTriggered: win.run("shape-rect") }
        FMenuItem { text: "Rectángulo redondeado"; onTriggered: win.run("shape-rounded") }
        FMenuItem { text: "Elipse"; onTriggered: win.run("shape-ellipse") }
        FMenuItem { text: "Triángulo"; onTriggered: win.run("shape-triangle") }
        FMenuItem { text: "Rombo"; onTriggered: win.run("shape-diamond") }
        FMenuItem { text: "Estrella"; onTriggered: win.run("shape-star") }
        FMenuItem { text: "Flecha de bloque"; onTriggered: win.run("shape-arrow") }
        FSeparator {}
        FMenuItem { text: "Línea"; onTriggered: win.run("shape-line") }
        FMenuItem { text: "Flecha"; onTriggered: win.run("shape-connector") }
    }
    FMenu {
        id: exportMenu
        width: 260
        FMenuItem { text: "Guardar"; onTriggered: win.run("save") }
        FMenuItem { text: "Guardar como…"; onTriggered: win.run("saveAs") }
        FMenuItem { text: "Versiones…"; onTriggered: win.run("versions") }
        FSeparator {}
        FMenuItem { text: "Exportar como PDF…"; onTriggered: win.run("exportPdf") }
        FMenuItem { text: "Exportar una imagen por diapositiva…"; onTriggered: win.run("exportPng") }
        FSeparator {}
        FMenuItem { text: "Abrir…"; onTriggered: win.run("open") }
        FMenuItem { text: "Nueva presentación"; onTriggered: win.run("new") }
    }

    CommandPalette {
        id: palette
        actions: win.actions
        onChosen: id => win.run(id)
    }
    VersionsPanel { id: versionsPanel }
    ConfirmDialog { id: confirmDialog }

    // Leaving with changes not saved.
    Popup {
        id: leaveDialog
        property bool decided: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        padding: 18
        modal: true
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#40000000" }
        background: Rectangle {
            radius: 12
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: Column {
            spacing: 10
            Text {
                width: parent.width
                text: "¿Guardar los cambios de «" + win.deck.name + "»?"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                text: "Si no los guardas, se perderán."
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
                    onClicked: {
                        leaveDialog.decided = true
                        leaveDialog.close()
                        win.close()
                    }
                }
                Row {
                    anchors.right: parent.right
                    spacing: 8
                    DialogButton { text: "Cancelar"; onClicked: leaveDialog.close() }
                    DialogButton {
                        text: "Guardar"
                        primary: true
                        onClicked: {
                            leaveDialog.close()
                            const leave = () => { leaveDialog.decided = true; win.close() }
                            if (win.deck.path === "") {
                                saveDialog.then = leave
                                saveDialog.open()
                            } else if (App.save(win.deck.path)) {
                                leave()
                            }
                        }
                    }
                }
            }
        }
    }

    // Every key there is, by section.
    Popup {
        id: keysPanel
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(720, parent.width - 60)
        height: Math.min(640, parent.height - 60)
        padding: 0
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: "#40000000" }
        background: Rectangle {
            radius: 12
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: Item {
            Text {
                x: 18
                y: 16
                text: "Atajos de teclado"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            TButton {
                anchors.right: parent.right
                anchors.rightMargin: 10
                y: 11
                symbol: "x"
                onClicked: keysPanel.close()
            }
            Flickable {
                anchors.fill: parent
                anchors.topMargin: 50
                anchors.leftMargin: 18
                anchors.rightMargin: 10
                anchors.bottomMargin: 12
                contentHeight: keysFlow.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 7 }
                Flow {
                    id: keysFlow
                    width: parent.width - 10
                    spacing: 0
                    Repeater {
                        model: win.actions.filter(entry => (entry.keys || "") !== "")
                        Item {
                            required property var modelData
                            width: Math.floor(keysFlow.width / 2)
                            height: 26
                            Text {
                                width: parent.width - 150
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title.replace("…", "")
                                color: Theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 24
                                anchors.verticalCenter: parent.verticalCenter
                                text: Theme.keys(modelData.keys)
                                color: Theme.dim
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Test runs ---------------------------------------------------------------------

    Timer {
        id: testStep
        interval: 300
        onTriggered: {
            for (const step of App.testActions)
                win.runAction(step.trim())
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 900
        onTriggered: App.saveShot(win)
    }
}
