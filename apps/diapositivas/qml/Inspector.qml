import QtQuick
import QtQuick.Controls.Basic

/// «Formato»: everything about what is selected, about the slide, and about the look of the deck.
Rectangle {
    id: panel
    property var ui
    readonly property var deck: App.deck
    readonly property var chosen: deck.selection
    readonly property var it: { deck.revision; return chosen.length > 0 ? deck.item(chosen[0]) : ({}) }
    readonly property var info: { deck.revision; return deck.slide(deck.current) }
    readonly property var swatches: { deck.revision; return deck.swatches() }
    readonly property var master: { deck.revision; return deck.master() }
    readonly property bool texty: chosen.length > 0 && it.texty === true
    readonly property bool shapely: chosen.length > 0 && !it.line && it.type !== "image" && it.type !== "table"
    color: Theme.chrome

    function format(key, value) {
        ui.format(key, value)
    }

    Rectangle { width: 1; height: parent.height; color: Theme.line }

    Segmented {
        id: tabs
        y: 10
        anchors.horizontalCenter: parent.horizontalCenter
        options: [{key: "format", label: "Formato"}, {key: "slide", label: "Diapositiva"}, {key: "theme", label: "Tema"}]
        current: panel.ui.inspectorTab
        onPicked: key => panel.ui.inspectorTab = key
    }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: 50
        anchors.leftMargin: 1
        contentHeight: (panel.ui.inspectorTab === "format" ? formatPage.height : panel.ui.inspectorTab === "slide" ? slidePage.height : themePage.height) + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { width: 8 }

        // --- What is selected ---
        Column {
            id: formatPage
            visible: panel.ui.inspectorTab === "format"
            x: 14
            width: parent.width - 28
            spacing: 12

            Column {
                visible: panel.chosen.length === 0
                width: parent.width
                topPadding: 60
                spacing: 8
                Sym { anchors.horizontalCenter: parent.horizontalCenter; name: "shapes"; size: 28; color: Theme.faint }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "Selecciona algo de la diapositiva para darle formato"
                    color: Theme.dim
                    font.pixelSize: 13
                }
            }

            Group {
                visible: panel.texty
                title: "Texto"
                FCombo {
                    width: parent.width
                    model: panel.deck.fonts()
                    currentIndex: model.indexOf(panel.it.font || "")
                    displayText: panel.it.font || ""
                    onActivated: index => panel.format("font", model[index])
                }
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        spacing: 2
                        TButton { symbol: "bold"; checked: panel.it.bold === true; onClicked: panel.ui.run("bold") }
                        TButton { symbol: "italic"; checked: panel.it.italic === true; onClicked: panel.ui.run("italic") }
                        TButton { symbol: "underline"; checked: panel.it.underline === true; onClicked: panel.ui.run("underline") }
                        TButton { symbol: "strikethrough"; checked: panel.it.strike === true; onClicked: panel.ui.run("strike") }
                    }
                    NumBox {
                        anchors.right: parent.right
                        value: panel.it.size || 0
                        unit: " pt"
                        onEdited: number => panel.format("size", number)
                    }
                }
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        spacing: 2
                        TButton { symbol: "text-align-start"; checked: panel.it.align === 0; onClicked: panel.ui.format("align", 0) }
                        TButton { symbol: "text-align-center"; checked: panel.it.align === 1; onClicked: panel.ui.format("align", 1) }
                        TButton { symbol: "text-align-end"; checked: panel.it.align === 2; onClicked: panel.ui.format("align", 2) }
                        TButton { symbol: "text-align-justify"; checked: panel.it.align === 3; onClicked: panel.ui.format("align", 3) }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: 2
                        TButton { symbol: "arrow-up-to-line"; checked: panel.it.valign === 0; onClicked: panel.deck.setProp("valign", 0) }
                        TButton { symbol: "fold-vertical"; checked: panel.it.valign === 1; onClicked: panel.deck.setProp("valign", 1) }
                        TButton { symbol: "arrow-down-to-line"; checked: panel.it.valign === 2; onClicked: panel.deck.setProp("valign", 2) }
                    }
                }
                Labeled {
                    label: "Viñetas"
                    Toggle { checked: panel.it.bullet === true; onToggled: value => panel.ui.format("bullet", value) }
                }
                Text {
                    text: "Color"
                    color: Theme.dim
                    font.pixelSize: 12
                }
                ColorWell {
                    allowNone: false
                    swatches: panel.swatches
                    value: panel.it.color || ""
                    shown: panel.it.color || ""
                    onPicked: color => panel.format("color", color)
                }
            }

            Group {
                visible: panel.shapely
                title: "Relleno"
                ColorWell {
                    swatches: panel.swatches
                    value: panel.it.fill || ""
                    shown: panel.it.fillColor || ""
                    onPicked: color => panel.deck.setProp("fill", color)
                }
            }
            Group {
                visible: panel.chosen.length > 0 && panel.it.type === "shape" && !panel.it.line
                title: "Forma"
                Row {
                    spacing: 2
                    Repeater {
                        model: [["rect", "square"], ["rounded", "square-round-corner"], ["ellipse", "circle"], ["triangle", "triangle"], ["diamond", "diamond"], ["star", "star"], ["arrow", "arrow-right"]]
                        TButton {
                            required property var modelData
                            symbol: modelData[1]
                            checked: panel.it.shape === modelData[0]
                            onClicked: panel.deck.setProp("shape", modelData[0])
                        }
                    }
                }
            }
            Group {
                visible: panel.chosen.length > 0 && panel.it.type !== "table"
                title: panel.it.line ? "Línea" : "Borde"
                ColorWell {
                    allowNone: !panel.it.line
                    swatches: panel.swatches
                    value: panel.it.stroke || ""
                    shown: panel.it.strokeColor || ""
                    onPicked: color => panel.deck.setProp("stroke", color)
                }
                Labeled {
                    label: "Grosor"
                    NumBox { value: panel.it.strokeWidth || 0; unit: " pt"; decimals: 1; onEdited: number => panel.deck.setProp("strokeWidth", number) }
                }
                Labeled {
                    visible: panel.it.line === true
                    label: "Punta de flecha"
                    Toggle { checked: panel.it.arrow === true; onToggled: value => panel.deck.setProp("arrow", value) }
                }
            }
            Group {
                visible: panel.chosen.length > 0 && panel.it.type === "table"
                title: "Tabla"
                Labeled {
                    label: "Filas"
                    NumBox { value: panel.it.rows || 0; onEdited: number => panel.deck.setTableSize(panel.it.id, Math.max(1, Math.min(20, Math.round(number))), panel.it.cols) }
                }
                Labeled {
                    label: "Columnas"
                    NumBox { value: panel.it.cols || 0; onEdited: number => panel.deck.setTableSize(panel.it.id, panel.it.rows, Math.max(1, Math.min(12, Math.round(number)))) }
                }
                Text { text: "Color de la cabecera"; color: Theme.text; font.pixelSize: 13 }
                ColorWell {
                    allowNone: false
                    swatches: panel.swatches
                    value: panel.it.fill || ""
                    shown: panel.it.fillColor || ""
                    onPicked: color => panel.deck.setProp("fill", color)
                }
                Text { text: "Celdas"; color: Theme.text; font.pixelSize: 13 }
                Grid {
                    id: cells
                    width: parent.width
                    columns: Math.max(1, panel.it.cols || 1)
                    spacing: 3
                    Repeater {
                        model: panel.it.type === "table" ? (panel.it.rows || 0) * (panel.it.cols || 0) : 0
                        Field {
                            required property int index
                            readonly property int row: Math.floor(index / cells.columns)
                            readonly property int col: index % cells.columns
                            width: (cells.width - (cells.columns - 1) * 3) / cells.columns
                            implicitHeight: 24
                            leftPadding: 5
                            rightPadding: 5
                            font.pixelSize: 12
                            font.weight: row === 0 ? Font.DemiBold : Font.Normal
                            text: (panel.it.cells || [])[index] || ""
                            onEditingFinished: if (text !== ((panel.it.cells || [])[index] || "")) panel.deck.setCell(panel.it.id, row, col, text)
                        }
                    }
                }
            }
            Group {
                visible: panel.chosen.length > 0
                title: "Aspecto"
                Labeled {
                    label: "Sombra"
                    Toggle { checked: panel.it.shadow === true; onToggled: value => panel.deck.setProp("shadow", value) }
                }
                Labeled {
                    label: "Opacidad"
                    Slider {
                        id: opacitySlider
                        width: 96
                        height: 20
                        from: 0
                        to: 1
                        value: panel.it.opacity === undefined ? 1 : panel.it.opacity
                        onMoved: panel.deck.setProp("opacity", value)
                        background: Rectangle {
                            y: 8
                            width: opacitySlider.width
                            height: 4
                            radius: 2
                            color: Theme.field
                            Rectangle { width: opacitySlider.visualPosition * parent.width; height: 4; radius: 2; color: Theme.tint }
                        }
                        handle: Rectangle {
                            x: opacitySlider.visualPosition * (opacitySlider.width - width)
                            y: 3
                            width: 14
                            height: 14
                            radius: 7
                            color: "#ffffff"
                            border.color: Theme.border
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        horizontalAlignment: Text.AlignRight
                        text: Math.round((panel.it.opacity === undefined ? 1 : panel.it.opacity) * 100) + " %"
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                }
            }
            Group {
                visible: panel.chosen.length > 0
                title: "Posición y tamaño"
                Item {
                    width: parent.width
                    height: 28
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "X"; color: Theme.dim; font.pixelSize: 12 }
                    NumBox { x: 22; width: 74; value: panel.it.x || 0; onEdited: number => panel.deck.setProp("x", number) }
                    Text { x: parent.width / 2 + 6; anchors.verticalCenter: parent.verticalCenter; text: "Y"; color: Theme.dim; font.pixelSize: 12 }
                    NumBox { anchors.right: parent.right; width: 74; value: panel.it.y || 0; onEdited: number => panel.deck.setProp("y", number) }
                }
                Item {
                    width: parent.width
                    height: 28
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "An"; color: Theme.dim; font.pixelSize: 12 }
                    NumBox { x: 22; width: 74; value: panel.it.w || 0; onEdited: number => panel.deck.setProp("w", number) }
                    Text { x: parent.width / 2 + 6; anchors.verticalCenter: parent.verticalCenter; text: "Al"; color: Theme.dim; font.pixelSize: 12 }
                    NumBox { anchors.right: parent.right; width: 74; value: panel.it.h || 0; onEdited: number => panel.deck.setProp("h", number) }
                }
                Labeled {
                    visible: panel.it.line !== true
                    label: "Giro"
                    NumBox { value: panel.it.rotation || 0; unit: "°"; onEdited: number => panel.deck.setProp("rotation", number) }
                }
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        spacing: 2
                        TButton { symbol: "bring-to-front"; onClicked: panel.ui.run("front") }
                        TButton { symbol: "chevrons-up"; onClicked: panel.ui.run("forward") }
                        TButton { symbol: "chevrons-down"; onClicked: panel.ui.run("backward") }
                        TButton { symbol: "send-to-back"; onClicked: panel.ui.run("back") }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: 2
                        TButton { symbol: "flip-horizontal-2"; checked: panel.it.flipH === true; onClicked: panel.ui.run("flipH") }
                        TButton { symbol: "flip-vertical-2"; checked: panel.it.flipV === true; onClicked: panel.ui.run("flipV") }
                    }
                }
            }
            Group {
                visible: panel.chosen.length > 0
                title: "Animación de entrada"
                Segmented {
                    options: [{key: "", label: "Ninguna"}, {key: "appear", label: "Aparecer"}, {key: "slide", label: "Deslizar"}]
                    current: panel.it.anim || ""
                    onPicked: key => panel.deck.setProp("anim", key)
                }
            }
        }

        // --- The slide ---
        Column {
            id: slidePage
            visible: panel.ui.inspectorTab === "slide"
            x: 14
            width: parent.width - 28
            spacing: 12
            Group {
                title: "Diseño"
                Grid {
                    id: layouts
                    width: parent.width
                    columns: 3
                    spacing: 8
                    Repeater {
                        model: panel.deck.layouts()
                        Column {
                            id: tile
                            required property var modelData
                            readonly property bool on: panel.info.layout === modelData.id
                            width: (layouts.width - 16) / 3
                            spacing: 4
                            Rectangle {
                                width: parent.width
                                height: Math.round(width * 0.6)
                                radius: 5
                                color: Theme.dark ? "#3a3a3c" : "#ffffff"
                                border.color: tile.on ? Theme.tint : Theme.border
                                border.width: tile.on ? 2 : 1
                                // A sketch of where things go.
                                readonly property color ink: Theme.dark ? "#8e8e93" : "#c7c7cc"
                                readonly property string kind: tile.modelData.id
                                Rectangle {
                                    visible: parent.kind !== "blank"
                                    x: parent.kind === "title" ? parent.width * 0.2 : 7
                                    y: parent.kind === "title" ? parent.height * 0.36 : parent.kind === "picture" ? parent.height - 12 : 7
                                    width: parent.kind === "title" ? parent.width * 0.6 : parent.width * 0.55
                                    height: 4
                                    radius: 2
                                    color: Theme.dim
                                }
                                Rectangle {
                                    visible: parent.kind === "title"
                                    x: parent.width * 0.3
                                    y: parent.height * 0.36 + 8
                                    width: parent.width * 0.4
                                    height: 3
                                    radius: 1.5
                                    color: parent.ink
                                }
                                Rectangle {
                                    visible: parent.kind === "content" || parent.kind === "columns"
                                    x: 7
                                    y: 16
                                    width: parent.kind === "columns" ? (parent.width - 19) / 2 : parent.width - 14
                                    height: parent.height - 23
                                    radius: 2
                                    color: parent.ink
                                }
                                Rectangle {
                                    visible: parent.kind === "columns"
                                    x: parent.width / 2 + 2.5
                                    y: 16
                                    width: (parent.width - 19) / 2
                                    height: parent.height - 23
                                    radius: 2
                                    color: parent.ink
                                }
                                Rectangle {
                                    visible: parent.kind === "picture"
                                    x: 7
                                    y: 7
                                    width: parent.width - 14
                                    height: parent.height - 24
                                    radius: 2
                                    color: parent.ink
                                }
                                MouseArea { anchors.fill: parent; onClicked: panel.ui.run("layout-" + tile.modelData.id) }
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: tile.modelData.name
                                color: tile.on ? Theme.text : Theme.dim
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
            Group {
                title: "Transición"
                FCombo {
                    readonly property var keys: ["", "dissolve", "push", "fade"]
                    width: parent.width
                    model: ["Ninguna", "Disolver", "Empujar", "Fundido a negro"]
                    currentIndex: Math.max(0, keys.indexOf(panel.info.transition || ""))
                    onActivated: index => panel.deck.setSlide(panel.deck.current, "transition", keys[index])
                }
            }
            Group {
                title: "Fondo"
                ColorWell {
                    swatches: panel.swatches
                    value: panel.info.background || ""
                    shown: panel.info.background ? (panel.swatches.find(swatch => swatch.token === panel.info.background) || {color: panel.info.background}).color : ""
                    onPicked: color => panel.deck.setSlide(panel.deck.current, "background", color)
                }
                Text {
                    width: parent.width
                    text: "Sin color, la diapositiva lleva el fondo del tema."
                    color: Theme.dim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
            Group {
                title: "Al presentar"
                Labeled {
                    label: "Omitir esta diapositiva"
                    Toggle { checked: panel.info.hidden === true; onToggled: panel.ui.run("slideHide") }
                }
            }
            Group {
                title: "Notas del orador"
                Rectangle {
                    width: parent.width
                    height: 150
                    radius: 7
                    color: Theme.field
                    border.color: notes.activeFocus ? Theme.accent : "transparent"
                    Flickable {
                        id: notesScroll
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { width: 7 }
                        TextArea.flickable: TextArea {
                            id: notes
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            color: Theme.text
                            font.pixelSize: 13
                            selectionColor: Theme.accent
                            selectedTextColor: "#ffffff"
                            selectByMouse: true
                            padding: 7
                            background: null
                            placeholderText: "Lo que quieres recordar al contarla"
                            placeholderTextColor: Theme.faint
                            property bool mine: false
                            readonly property string stored: panel.info.notes || ""
                            onStoredChanged: if (text !== stored) { mine = true; text = stored; mine = false }
                            Component.onCompleted: { mine = true; text = stored; mine = false }
                            onTextChanged: if (!mine && text !== stored) panel.deck.setSlide(panel.deck.current, "notes", text)
                        }
                    }
                }
            }
        }

        // --- The look of the whole deck ---
        Column {
            id: themePage
            visible: panel.ui.inspectorTab === "theme"
            x: 14
            width: parent.width - 28
            spacing: 12
            Group {
                title: "Tema"
                Grid {
                    id: themes
                    width: parent.width
                    columns: 2
                    spacing: 10
                    Repeater {
                        model: panel.deck.themes()
                        Column {
                            id: card
                            required property var modelData
                            readonly property bool on: panel.deck.theme === modelData.id
                            width: (themes.width - 10) / 2
                            spacing: 4
                            Rectangle {
                                width: parent.width
                                height: Math.round((width - 6) * panel.deck.height / panel.deck.width) + 6
                                radius: 6
                                color: "transparent"
                                border.color: card.on ? Theme.tint : ((panel.ui.tryTheme === card.modelData.id) ? Theme.dim : Theme.border)
                                border.width: card.on || (panel.ui.tryTheme === card.modelData.id) ? 2 : 1
                                // This very slide, as it would look.
                                SlideView {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    deck: panel.deck
                                    index: panel.deck.current
                                    theme: card.modelData.id
                                }
                                MouseArea {
                                    id: cardArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    // Passing over a theme tries it on the whole window; leaving puts it back.
                                    onContainsMouseChanged: {
                                        if (containsMouse)
                                            panel.ui.tryTheme = card.modelData.id
                                        else if (panel.ui.tryTheme === card.modelData.id)
                                            panel.ui.tryTheme = ""
                                    }
                                    onClicked: {
                                        panel.ui.tryTheme = ""
                                        panel.deck.setTheme(card.modelData.id)
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: card.modelData.name
                                color: card.on ? Theme.text : Theme.dim
                                font.pixelSize: 11
                                font.weight: card.on ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }
            Group {
                title: "Formato"
                Segmented {
                    options: [{key: "16:9", label: "Panorámico 16:9"}, {key: "4:3", label: "Clásico 4:3"}]
                    current: panel.deck.aspect
                    onPicked: key => panel.deck.setAspect(key)
                }
            }
            Group {
                title: "Pie de las diapositivas"
                Field {
                    width: parent.width
                    placeholderText: "Texto del pie"
                    text: panel.master.footer || ""
                    onEditingFinished: {
                        if (text === (panel.master.footer || ""))
                            return
                        const next = Object.assign({}, panel.master)
                        next.footer = text
                        panel.deck.setMaster(next)
                    }
                }
                Labeled {
                    label: "Número de diapositiva"
                    Toggle {
                        checked: panel.master.numbers === true
                        onToggled: value => {
                            const next = Object.assign({}, panel.master)
                            next.numbers = value
                            panel.deck.setMaster(next)
                        }
                    }
                }
            }
        }
    }
}
