import QtQuick
import QtQuick.Controls.Basic

/// The panel on the right: everything about how the text under the cursor
/// looks, and about the sheet it is on. It changes with what is selected.
Rectangle {
    id: panel
    readonly property var editor: App.editor
    readonly property var look: App.editor.format
    readonly property var sheet: App.editor.setup
    /// format or document.
    property string tab: "format"
    property int goal: 0
    signal act(string id)
    signal goalEdited(int words)
    color: Theme.chrome

    readonly property var inks: ["#1d1d1f", "#6e6e73", "#d70015", "#c93400", "#b25000", "#248a3d", "#0071e3", "#8944ab"]
    readonly property var markers: ["", "#ffe066", "#b8f2c2", "#bfe3ff", "#ffc9de", "#e2d1ff"]
    readonly property var spacings: [100, 115, 150, 200]

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }
    Segmented {
        id: tabs
        y: 10
        anchors.horizontalCenter: parent.horizontalCenter
        options: [{key: "format", label: "Formato"}, {key: "document", label: "Documento"}]
        current: panel.tab
        onPicked: key => panel.tab = key
    }
    Rectangle {
        y: 48
        x: 1
        width: parent.width - 1
        height: 1
        color: Theme.line
    }

    component Group: Column {
        x: 16
        width: panel.width - 32
        spacing: 8
    }
    component Divider: Rectangle {
        x: 16
        width: panel.width - 32
        height: 1
        color: Theme.line
    }
    component Labelled: Item {
        property string label
        default property alias content: slot.data
        width: parent.width
        height: 28
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: Theme.text
            font.pixelSize: 13
        }
        Row {
            id: slot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
        }
    }

    Flickable {
        id: flick
        x: 1
        y: 49
        width: parent.width - 1
        height: parent.height - y
        contentHeight: (panel.tab === "format" ? format.height : document.height) + 28
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        // --- Formato -------------------------------------------------------------
        Column {
            id: format
            visible: panel.tab === "format"
            y: 14
            width: parent.width
            spacing: 14

            Group {
                visible: panel.look.table
                SectionLabel { text: "Tabla · " + panel.look.rows + " × " + panel.look.columns }
                Labelled {
                    label: "Filas"
                    TButton { symbol: "plus"; onClicked: panel.act("rowBelow") }
                    TButton { symbol: "minus"; active: panel.look.rows > 1; onClicked: panel.act("removeRow") }
                }
                Labelled {
                    label: "Columnas"
                    TButton { symbol: "plus"; onClicked: panel.act("columnAfter") }
                    TButton { symbol: "minus"; active: panel.look.columns > 1; onClicked: panel.act("removeColumn") }
                }
                DialogButton {
                    width: parent.width
                    text: "Eliminar la tabla"
                    danger: true
                    onClicked: panel.act("removeTable")
                }
            }
            Divider { visible: panel.look.table }

            Group {
                visible: panel.look.image
                SectionLabel { text: "Imagen" }
                Labelled {
                    label: "Ancho"
                    NumField {
                        value: panel.look.imageWidth
                        decimals: 1
                        unit: "cm"
                        from: 0.5
                        to: 40
                        onEdited: value => panel.editor.setImageWidth(value)
                    }
                }
                Text {
                    text: "Alto: " + Number(panel.look.imageHeight).toFixed(1).replace(".", ",") + " cm (sigue al ancho)"
                    color: Theme.dim
                    font.pixelSize: 12
                }
            }
            Divider { visible: panel.look.image }

            Group {
                visible: panel.look.link !== ""
                SectionLabel { text: "Enlace" }
                Field {
                    width: parent.width
                    text: panel.look.link
                    onAccepted: panel.editor.setLink(text)
                }
                Row {
                    spacing: 8
                    DialogButton {
                        text: "Abrir"
                        onClicked: App.openLink(panel.look.link)
                    }
                    DialogButton {
                        text: "Quitar"
                        danger: true
                        onClicked: panel.act("removeLink")
                    }
                }
            }
            Divider { visible: panel.look.link !== "" }

            Group {
                spacing: 2
                SectionLabel { text: "Estilo"; bottomPadding: 6 }
                Repeater {
                    model: panel.editor.styles
                    Rectangle {
                        id: style
                        required property var modelData
                        readonly property bool on: panel.look.style === modelData.id
                        x: -8
                        width: parent.width + 16
                        height: 30
                        radius: 6
                        color: on ? Theme.sideSelection : (over.containsMouse ? Theme.hover : "transparent")
                        Text {
                            x: 8
                            width: parent.width - 36
                            anchors.verticalCenter: parent.verticalCenter
                            text: style.modelData.name
                            elide: Text.ElideRight
                            color: style.modelData.id === "quote" ? Theme.dim : Theme.text
                            font.family: style.modelData.family
                            font.pixelSize: Math.max(12, Math.min(18, Math.round(style.modelData.size * 0.72 + 5)))
                            font.bold: style.modelData.bold
                            font.italic: style.modelData.italic
                        }
                        Sym {
                            visible: style.on
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            name: "check"
                            size: 13
                            color: Theme.tint
                        }
                        MouseArea {
                            id: over
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: panel.editor.applyStyle(style.modelData.id)
                        }
                    }
                }
            }
            Divider {}

            Group {
                SectionLabel { text: "Fuente" }
                FCombo {
                    id: family
                    width: parent.width
                    model: App.families
                    displayText: panel.look.family
                    onActivated: index => panel.editor.setFamily(App.families[index])
                    onPressedChanged: if (pressed) currentIndex = App.families.indexOf(panel.look.family)
                }
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        spacing: 2
                        TButton { y: 1; symbol: "bold"; checked: panel.look.bold; onClicked: panel.act("bold") }
                        TButton { y: 1; symbol: "italic"; checked: panel.look.italic; onClicked: panel.act("italic") }
                        TButton { y: 1; symbol: "underline"; checked: panel.look.underline; onClicked: panel.act("underline") }
                        TButton { y: 1; symbol: "strikethrough"; checked: panel.look.strike; onClicked: panel.act("strike") }
                    }
                    NumField {
                        anchors.right: parent.right
                        width: 72
                        value: panel.look.size
                        decimals: 1
                        unit: "pt"
                        from: 4
                        to: 200
                        onEdited: value => panel.editor.setPointSize(value)
                    }
                }
                Labelled {
                    label: "Posición"
                    TButton { symbol: "superscript"; checked: panel.look.vertical === 1; onClicked: panel.act("superscript") }
                    TButton { symbol: "subscript"; checked: panel.look.vertical === 2; onClicked: panel.act("subscript") }
                }
                Text {
                    text: "Color"
                    color: Theme.text
                    font.pixelSize: 13
                }
                ColorRow {
                    colors: panel.inks
                    current: panel.look.color
                    onPicked: color => panel.editor.setColor(color)
                }
                Text {
                    topPadding: 2
                    text: "Resaltado"
                    color: Theme.text
                    font.pixelSize: 13
                }
                ColorRow {
                    colors: panel.markers
                    current: panel.look.highlight
                    onPicked: color => color === "" ? panel.editor.clearHighlight() : panel.editor.setHighlight(color)
                }
            }
            Divider {}

            Group {
                SectionLabel { text: "Párrafo" }
                Item {
                    width: parent.width
                    height: 28
                    Row {
                        spacing: 2
                        TButton { y: 1; symbol: "text-align-start"; checked: panel.look.alignment === "left"; onClicked: panel.act("alignLeft") }
                        TButton { y: 1; symbol: "text-align-center"; checked: panel.look.alignment === "center"; onClicked: panel.act("alignCenter") }
                        TButton { y: 1; symbol: "text-align-end"; checked: panel.look.alignment === "right"; onClicked: panel.act("alignRight") }
                        TButton { y: 1; symbol: "text-align-justify"; checked: panel.look.alignment === "justify"; onClicked: panel.act("alignJustify") }
                    }
                    Row {
                        anchors.right: parent.right
                        spacing: 2
                        TButton { y: 1; symbol: "list"; checked: panel.look.list === 1; onClicked: panel.act("bullets") }
                        TButton { y: 1; symbol: "list-ordered"; checked: panel.look.list === 2; onClicked: panel.act("numbers") }
                    }
                }
                Labelled {
                    label: "Sangrar"
                    TButton { symbol: "list-indent-decrease"; onClicked: panel.act("indentLess") }
                    TButton { symbol: "list-indent-increase"; onClicked: panel.act("indentMore") }
                }
                Labelled {
                    label: "Interlineado"
                    FCombo {
                        width: 96
                        implicitWidth: 96
                        model: ["Sencillo", "1,15", "1,5", "Doble"]
                        displayText: {
                            const at = panel.spacings.indexOf(panel.look.lineHeight)
                            return at >= 0 ? model[at] : String(panel.look.lineHeight / 100).replace(".", ",")
                        }
                        onActivated: index => panel.editor.setLineHeight(panel.spacings[index])
                    }
                }
                Labelled {
                    label: "Antes"
                    NumField {
                        value: panel.look.before
                        unit: "pt"
                        to: 200
                        onEdited: value => panel.editor.setSpacing(value, panel.look.after)
                    }
                }
                Labelled {
                    label: "Después"
                    NumField {
                        value: panel.look.after
                        unit: "pt"
                        to: 200
                        onEdited: value => panel.editor.setSpacing(panel.look.before, value)
                    }
                }
                Labelled {
                    label: "Sangría izquierda"
                    NumField {
                        value: panel.look.left
                        decimals: 2
                        unit: "cm"
                        to: 15
                        onEdited: value => panel.editor.setIndents(value, panel.look.right, panel.look.first)
                    }
                }
                Labelled {
                    label: "Primera línea"
                    NumField {
                        value: panel.look.first
                        decimals: 2
                        unit: "cm"
                        from: -10
                        to: 15
                        onEdited: value => panel.editor.setIndents(panel.look.left, panel.look.right, value)
                    }
                }
            }
        }

        // --- Documento -----------------------------------------------------------
        Column {
            id: document
            visible: panel.tab === "document"
            y: 14
            width: parent.width
            spacing: 14

            Group {
                SectionLabel { text: "Papel" }
                FCombo {
                    width: parent.width
                    model: panel.editor.papers
                    displayText: panel.sheet.paper
                    onActivated: index => panel.editor.setSetup({paper: panel.editor.papers[index]})
                }
                Segmented {
                    options: [{key: "portrait", label: "Vertical"}, {key: "landscape", label: "Horizontal"}]
                    current: panel.sheet.landscape ? "landscape" : "portrait"
                    onPicked: key => panel.editor.setSetup({landscape: key === "landscape"})
                }
            }
            Divider {}
            Group {
                SectionLabel { text: "Márgenes" }
                Grid {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Repeater {
                        model: [{key: "top", label: "Arriba"}, {key: "bottom", label: "Abajo"}, {key: "left", label: "Izquierda"}, {key: "right", label: "Derecha"}]
                        Column {
                            id: margin
                            required property var modelData
                            width: (panel.width - 32 - 12) / 2
                            spacing: 3
                            Text {
                                text: margin.modelData.label
                                color: Theme.dim
                                font.pixelSize: 12
                            }
                            NumField {
                                width: parent.width
                                value: panel.sheet[margin.modelData.key]
                                decimals: 2
                                unit: "cm"
                                to: 10
                                onEdited: value => {
                                    const change = {}
                                    change[margin.modelData.key] = value
                                    panel.editor.setSetup(change)
                                }
                            }
                        }
                    }
                }
            }
            Divider {}
            Group {
                SectionLabel { text: "Encabezado y pie" }
                Field {
                    width: parent.width
                    placeholderText: "Encabezado de cada página"
                    text: panel.sheet.header
                    onEditingFinished: panel.editor.setSetup({header: text})
                }
                Field {
                    width: parent.width
                    placeholderText: "Pie de cada página"
                    text: panel.sheet.footer
                    onEditingFinished: panel.editor.setSetup({footer: text})
                }
                Labelled {
                    label: "Número de página"
                    Toggle {
                        checked: panel.sheet.pageNumbers
                        onToggled: checked => panel.editor.setSetup({pageNumbers: checked})
                    }
                }
            }
            Divider {}
            Group {
                SectionLabel { text: "Escritura" }
                Labelled {
                    label: "Objetivo de palabras"
                    NumField {
                        width: 84
                        value: panel.goal
                        decimals: 0
                        to: 1000000
                        onEdited: value => panel.goalEdited(Math.round(value))
                    }
                }
                Labelled {
                    label: "Atajos de Markdown"
                    Toggle {
                        checked: panel.editor.markdown
                        onToggled: panel.act("markdown")
                    }
                }
                Text {
                    width: parent.width
                    text: "«# » hace un encabezado, «- » una lista, «1. » una numerada, «> » una cita, **así** negrita y ``` un bloque de código."
                    wrapMode: Text.Wrap
                    lineHeight: 1.25
                    color: Theme.dim
                    font.pixelSize: 12
                }
            }
        }
    }
}
