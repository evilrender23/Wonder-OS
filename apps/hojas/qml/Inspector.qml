import QtQuick
import QtQuick.Controls.Basic

/// «Formato»: how the selection looks, or the chart in hand.
Rectangle {
    id: panel
    required property var host
    readonly property var book: host.book
    readonly property var info: host.info
    readonly property var s: host.selection
    readonly property real inner: width - 28
    color: Theme.chrome
    clip: true

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    component Heading: Text {
        topPadding: 8
        color: Theme.dim
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
    component Line: Rectangle {
        width: panel.inner
        height: 1
        color: Theme.border
    }
    component Swatches: Flow {
        id: flow
        property var colors: []
        property string current: ""
        signal picked(string color)
        width: panel.inner
        spacing: 5
        Repeater {
            model: flow.colors
            Rectangle {
                required property string modelData
                readonly property bool on: flow.current.toLowerCase() === modelData
                width: 19
                height: 19
                radius: 9.5
                color: modelData === "" ? "transparent" : modelData
                border.color: on ? Theme.tint : Theme.border
                border.width: on ? 2 : 1
                // «None»: a stroke across.
                Rectangle {
                    visible: parent.modelData === ""
                    anchors.centerIn: parent
                    width: 15
                    height: 1.5
                    rotation: -45
                    color: "#d70015"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: flow.picked(parent.modelData)
                }
            }
        }
    }
    component Tool: TButton {
        width: 32
        height: 28
    }
    component Group: Rectangle {
        default property alias content: groupRow.data
        width: groupRow.implicitWidth + 4
        height: 30
        radius: 7
        color: Theme.field
        Row {
            id: groupRow
            x: 2
            y: 1
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 1
        contentHeight: (panel.host.chart !== null ? chartColumn.height : cellColumn.height) + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // --- A selection of cells ---------------------------------------------------
        Column {
            id: cellColumn
            visible: panel.host.chart === null
            x: 14
            y: 10
            width: panel.inner
            spacing: 9

            Text {
                text: "Formato"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Theme.dim
                font.pixelSize: 12
                text: {
                    const kinds = {number: "Número", date: "Fecha", text: "Texto", check: "Casilla", error: "Error"}
                    const kind = panel.info.formula ? "Fórmula" : (kinds[panel.info.kind] || "Vacía")
                    return panel.host.selectionName() + " · " + (panel.host.single ? kind : "rango")
                }
            }
            Line {}

            Heading { text: "NÚMERO" }
            FCombo {
                id: family
                readonly property var keys: ["general", "number", "currency", "percent", "date", "time", "scientific", "text"]
                width: parent.width
                model: ["General", "Número", "Moneda (€)", "Porcentaje", "Fecha", "Hora", "Científico", "Texto"]
                currentIndex: Math.max(0, keys.indexOf(panel.info.family === undefined ? "general" : panel.info.family))
                onActivated: index => panel.host.format(keys[index])
            }
            Row {
                spacing: 8
                visible: ["number", "currency", "percent", "scientific"].includes(panel.info.family)
                Text {
                    height: 28
                    verticalAlignment: Text.AlignVCenter
                    text: "Decimales"
                    color: Theme.text
                    font.pixelSize: 13
                }
                Group {
                    Tool { symbol: "minus"; size: 14; onClicked: panel.host.run("fewer-decimals") }
                    Text {
                        width: 26
                        height: 28
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: panel.info.decimals === undefined ? "" : panel.info.decimals
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    Tool { symbol: "plus"; size: 14; onClicked: panel.host.run("more-decimals") }
                }
            }
            Line {}

            Heading { text: "TEXTO" }
            Row {
                spacing: 8
                Group {
                    Tool { symbol: "bold"; checked: panel.info.bold === true; onClicked: panel.host.run("bold") }
                    Tool { symbol: "italic"; checked: panel.info.italic === true; onClicked: panel.host.run("italic") }
                    Tool { symbol: "underline"; checked: panel.info.underline === true; onClicked: panel.host.run("underline") }
                    Tool { symbol: "strikethrough"; checked: panel.info.strike === true; onClicked: panel.host.run("strike") }
                }
                FCombo {
                    readonly property var sizes: [9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 36]
                    width: 72
                    model: sizes
                    currentIndex: Math.max(0, sizes.indexOf(panel.info.size > 0 ? panel.info.size : 11))
                    onActivated: index => panel.host.style("size", sizes[index])
                }
            }
            Row {
                spacing: 8
                Group {
                    Tool { symbol: "align-left"; checked: panel.info.align === 1; onClicked: panel.host.run("align-left") }
                    Tool { symbol: "align-center"; checked: panel.info.align === 2; onClicked: panel.host.run("align-center") }
                    Tool { symbol: "align-right"; checked: panel.info.align === 3; onClicked: panel.host.run("align-right") }
                }
                Group {
                    Tool { symbol: "arrow-up-to-line"; checked: panel.info.valign === 2; onClicked: panel.host.style("valign", panel.info.valign === 2 ? 0 : 2) }
                    Tool { symbol: "fold-vertical"; checked: panel.info.valign === 1; onClicked: panel.host.style("valign", panel.info.valign === 1 ? 0 : 1) }
                    Tool { symbol: "wrap-text"; checked: panel.info.wrap === true; onClicked: panel.host.run("wrap") }
                }
            }
            Heading { text: "COLOR DEL TEXTO" }
            Swatches {
                colors: Colors.inks
                current: panel.info.color === undefined ? "" : panel.info.color
                onPicked: color => panel.host.style("color", color)
            }
            Line {}

            Heading { text: "RELLENO" }
            Swatches {
                colors: Colors.fills
                current: panel.info.fill === undefined ? "" : panel.info.fill
                onPicked: color => panel.host.style("fill", color)
            }
            Line {}

            Heading { text: "BORDES" }
            Row {
                spacing: 8
                Group {
                    Tool { symbol: "grid-2x2"; onClicked: panel.host.run("borders-all") }
                    Tool { symbol: "square"; onClicked: panel.host.run("borders-outline") }
                    Tool { symbol: "panel-bottom"; onClicked: panel.book.setBorders(panel.s.r1, panel.s.c1, panel.s.r2, panel.s.c2, "bottom") }
                    Tool { symbol: "panel-top"; onClicked: panel.book.setBorders(panel.s.r1, panel.s.c1, panel.s.r2, panel.s.c2, "top") }
                    Tool { symbol: "square-dashed"; onClicked: panel.host.run("borders-none") }
                }
            }
            Line {}

            Heading { text: "CELDAS" }
            Row {
                spacing: 8
                DialogButton {
                    text: panel.info.merged ? "Separar" : "Combinar"
                    active: panel.info.merged === true || !panel.host.single
                    onClicked: panel.host.run("merge")
                }
                DialogButton {
                    text: panel.book.freezeRows > 0 || panel.book.freezeCols > 0 ? "Soltar" : "Inmovilizar"
                    onClicked: panel.host.run("freeze")
                }
            }
            Line {}

            Heading { text: "TABLA" }
            Row {
                spacing: 8
                DialogButton {
                    text: panel.host.table.index !== undefined ? "Dejar de ser tabla" : "Convertir en tabla"
                    onClicked: panel.host.run("table")
                }
            }
            Row {
                visible: panel.host.table.index !== undefined
                spacing: 8
                Check {
                    on: panel.host.table.totals === true
                    onToggled: panel.host.run("table-totals")
                }
                Text {
                    height: 18
                    verticalAlignment: Text.AlignVCenter
                    text: "Fila de totales"
                    color: Theme.text
                    font.pixelSize: 13
                }
            }
            Line {}

            Heading { text: "FORMATO CONDICIONAL" }
            Repeater {
                model: { panel.book.revision; return panel.book.open ? panel.book.conditionals() : [] }
                Item {
                    id: rule
                    required property var modelData
                    required property int index
                    width: panel.inner
                    height: 30
                    Rectangle {
                        y: 6
                        width: 18
                        height: 18
                        radius: 4
                        color: rule.modelData.fill === "" ? "transparent" : rule.modelData.fill
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: "a"
                            color: rule.modelData.color === "" ? "#1d1d1f" : rule.modelData.color
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                    Column {
                        x: 26
                        width: parent.width - 54
                        anchors.verticalCenter: parent.verticalCenter
                        Text { width: parent.width; elide: Text.ElideRight; text: rule.modelData.text; color: Theme.text; font.pixelSize: 12 }
                        Text { text: rule.modelData.range; color: Theme.dim; font.pixelSize: 10 }
                    }
                    TButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 22
                        size: 13
                        symbol: "x"
                        tone: Theme.dim
                        onClicked: panel.book.removeConditional(rule.index)
                    }
                }
            }
            DialogButton {
                text: "Añadir una regla…"
                onClicked: panel.host.run("conditional")
            }
        }

        // --- A chart -----------------------------------------------------------------
        Column {
            id: chartColumn
            visible: panel.host.chart !== null
            x: 14
            y: 10
            width: panel.inner
            spacing: 9
            readonly property var chart: panel.host.chart === null ? ({}) : panel.host.chart

            Text {
                text: "Gráfico"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Line {}
            Heading { text: "TIPO" }
            Grid {
                columns: 6
                spacing: 4
                Repeater {
                    model: ["column", "bar", "line", "area", "pie", "scatter"]
                    Rectangle {
                        required property string modelData
                        readonly property bool on: chartColumn.chart.type === modelData
                        width: 35
                        height: 30
                        radius: 7
                        color: on ? Qt.alpha(Theme.tint, 0.18) : (typeArea.containsMouse ? Theme.sideSelection : Theme.field)
                        Sym {
                            anchors.centerIn: parent
                            name: "chart-" + parent.modelData
                            size: 16
                            color: parent.on ? Theme.tint : Theme.text
                        }
                        MouseArea {
                            id: typeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: panel.book.setChart(panel.host.chartId, "type", parent.modelData)
                        }
                    }
                }
            }
            Heading { text: "TÍTULO" }
            Field {
                width: parent.width
                text: chartColumn.chart.title === undefined ? "" : chartColumn.chart.title
                placeholderText: "Sin título"
                onEditingFinished: if (panel.host.chartId !== 0 && text !== chartColumn.chart.title) panel.book.setChart(panel.host.chartId, "title", text)
            }
            Heading { text: "DATOS" }
            Field {
                id: rangeField
                width: parent.width
                text: chartColumn.chart.range === undefined ? "" : chartColumn.chart.range
                wrong: text !== "" && !panel.book.parseName(text).ok
                onEditingFinished: if (panel.host.chartId !== 0 && !wrong && text !== chartColumn.chart.range) panel.book.setChart(panel.host.chartId, "range", text)
            }
            Repeater {
                model: [["headerRow", "Títulos en la primera fila"], ["headerCol", "Categorías en la primera columna"], ["legend", "Mostrar la leyenda"]]
                Row {
                    required property var modelData
                    spacing: 8
                    Check {
                        on: chartColumn.chart[parent.modelData[0]] === true
                        onToggled: panel.book.setChart(panel.host.chartId, parent.modelData[0], !on)
                    }
                    Text {
                        height: 18
                        verticalAlignment: Text.AlignVCenter
                        text: parent.modelData[1]
                        color: Theme.text
                        font.pixelSize: 13
                    }
                }
            }
            Line {}
            DialogButton {
                text: "Eliminar el gráfico"
                danger: true
                onClicked: panel.host.run("chart-remove")
            }
        }
    }
}
