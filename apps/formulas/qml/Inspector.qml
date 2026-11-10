import QtQuick
import QtQuick.Controls.Basic

/// «Formato», the right of the window: how the formulas are drawn, the ways
/// to take the one in hand elsewhere, and what the parser has to say.
Rectangle {
    id: panel
    /// The scale of the picture «Copiar como» puts on the clipboard.
    property real copyScale: 2
    signal run(string action)
    signal goTo(int position)
    color: Theme.chrome

    component Rule: Item {
        width: parent ? parent.width : 0
        height: 15
        Rectangle {
            y: 7
            width: parent.width
            height: 1
            color: Theme.line
        }
    }
    component CopyButton: Rectangle {
        id: copyButton
        property string label
        property string symbol
        signal clicked()
        width: (parent.width - 6) / 2
        height: 30
        radius: 7
        color: copyHit.containsMouse ? Theme.sideSelection : Theme.field
        Sym {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            name: copyButton.symbol
            size: 14
            color: Theme.dim
        }
        Text {
            x: 30
            anchors.verticalCenter: parent.verticalCenter
            text: copyButton.label
            color: Theme.text
            font.pixelSize: 12
        }
        MouseArea {
            id: copyHit
            anchors.fill: parent
            hoverEnabled: true
            onClicked: copyButton.clicked()
        }
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }
    Flickable {
        x: 1
        width: parent.width - 1
        height: parent.height
        contentHeight: column.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            x: 14
            y: 8
            width: parent.width - 28

            Heading { text: "Formato" }
            PropRow {
                label: "Tamaño"
                NumField {
                    value: App.size
                    unit: "pt"
                    decimals: 0
                    from: 6
                    to: 144
                    onEdited: value => App.size = value
                }
                TButton {
                    width: 26
                    symbol: "minus"
                    size: 13
                    onClicked: App.size = Math.max(6, App.size - 2)
                }
                TButton {
                    width: 26
                    symbol: "plus"
                    size: 13
                    onClicked: App.size = Math.min(144, App.size + 2)
                }
            }
            PropRow {
                label: "Color"
                ColorWell {
                    anchors.verticalCenter: parent.verticalCenter
                    value: App.color === "" ? "#000000" : App.color
                    mixed: App.color === ""
                    onPicked: color => App.color = color
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 76
                    elide: Text.ElideRight
                    text: App.color === "" ? "Automático" : App.color
                    color: Theme.dim
                    font.pixelSize: 12
                }
                TButton {
                    width: 26
                    symbol: "rotate-ccw"
                    size: 13
                    active: App.color !== ""
                    onClicked: App.color = ""
                }
            }
            PropRow {
                label: "Fuente"
                FCombo {
                    implicitWidth: parent.width
                    model: ["Automática"].concat(App.fonts)
                    currentIndex: App.font === "" ? 0 : Math.max(0, App.fonts.indexOf(App.font) + 1)
                    onActivated: index => App.font = index === 0 ? "" : App.fonts[index - 1]
                }
            }
            Text {
                width: parent.width
                topPadding: 2
                elide: Text.ElideRight
                text: "Se dibuja con " + App.fontInUse
                color: Theme.faint
                font.pixelSize: 11
            }

            Rule {}
            Heading { text: "Copiar como" }
            Grid {
                width: parent.width
                columns: 2
                spacing: 6
                CopyButton {
                    label: "Imagen PNG"
                    symbol: "image"
                    onClicked: panel.run("copy-png")
                }
                CopyButton {
                    label: "SVG"
                    symbol: "file-down"
                    onClicked: panel.run("copy-svg")
                }
                CopyButton {
                    label: "LaTeX"
                    symbol: "code"
                    onClicked: panel.run("copy-latex")
                }
                CopyButton {
                    label: "MathML"
                    symbol: "braces"
                    onClicked: panel.run("copy-mathml")
                }
            }
            Item { width: 1; height: 6 }
            PropRow {
                label: "Escala PNG"
                Segmented {
                    options: [{key: "1", label: "1×"}, {key: "2", label: "2×"}, {key: "3", label: "3×"}, {key: "4", label: "4×"}]
                    current: String(panel.copyScale)
                    onPicked: key => panel.copyScale = Number(key)
                }
            }
            Text {
                width: parent.width
                topPadding: 2
                wrapMode: Text.Wrap
                text: "La imagen va con el fondo transparente, lista para pegar en Textos o Diapositivas."
                color: Theme.faint
                font.pixelSize: 11
            }

            Rule {}
            Heading { text: App.errors.length === 0 ? "Sintaxis" : (App.errors.length === 1 ? "1 aviso" : App.errors.length + " avisos") }
            Row {
                visible: App.errors.length === 0
                spacing: 7
                Sym {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "check"
                    size: 14
                    color: Theme.dim
                }
                Text {
                    text: App.latex.trim() === "" ? "Aún no hay nada escrito." : "Todo se entiende."
                    color: Theme.dim
                    font.pixelSize: 12
                }
            }
            Repeater {
                model: App.errors
                Rectangle {
                    id: problem
                    required property var modelData
                    width: column.width
                    height: problemText.implicitHeight + 28
                    radius: 7
                    color: problemHit.containsMouse ? Theme.hover : "transparent"
                    Text {
                        x: 8
                        y: 6
                        text: "Línea " + problem.modelData.line + ", columna " + problem.modelData.column
                        color: Theme.dark ? "#ff6961" : "#d70015"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: problemText
                        x: 8
                        y: 22
                        width: parent.width - 16
                        wrapMode: Text.Wrap
                        text: problem.modelData.message
                        color: Theme.text
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: problemHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.goTo(problem.modelData.position)
                    }
                }
            }

            Rule {}
            Heading { text: "Documento" }
            PropRow {
                label: "Ecuaciones"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.equations.length
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
            PropRow {
                label: "Archivo"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: App.path === "" ? "Sin guardar" : App.fileName(App.path)
                    color: App.path === "" ? Theme.dim : Theme.text
                    font.pixelSize: 12
                }
            }
        }
    }
}
