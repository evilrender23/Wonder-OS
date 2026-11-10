import QtQuick
import QtQuick.Controls.Basic

/// «Formato», the right of the window: everything about what is selected,
/// or about the drawing when nothing is.
Rectangle {
    id: panel
    readonly property var info: App.editor.info
    readonly property bool some: (info.count ?? 0) > 0
    signal run(string action)
    color: Theme.chrome

    function mixed(key) {
        return (info.mixed ?? []).includes(key)
    }
    function set(key, value) {
        App.editor.setProp(key, value)
    }

    component Rule: Item {
        width: parent ? parent.width : 0
        height: 13
        Rectangle {
            y: 6
            width: parent.width
            height: 1
            color: Theme.line
        }
    }
    component Mini: TButton {
        opacity: enabled ? 1 : 0.35
        width: 28
        height: 26
    }
    component Pick: FCombo {
        id: pick
        /// [{key, label}]
        property var options: []
        property string value
        property bool mixed: false
        signal chosen(string key)
        implicitWidth: 144
        displayText: mixed ? "Varios" : currentText
        model: options.map(o => o.label)
        currentIndex: Math.max(0, options.findIndex(o => o.key === value))
        onActivated: index => pick.chosen(options[index].key)
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }
    Text {
        x: 16
        y: 14
        text: "Formato"
        color: Theme.text
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 15
        width: parent.width - 100
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: panel.some ? panel.info.name : "Dibujo"
        color: Theme.dim
        font.pixelSize: 12
    }

    Flickable {
        x: 1
        y: 42
        width: parent.width - 1
        height: parent.height - y
        contentHeight: body.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator {}
        Column {
            id: body
            x: 15
            width: parent.width - 30
            spacing: 4

            // --- Nothing selected: the drawing ---
            Column {
                visible: !panel.some
                width: parent.width
                spacing: 4
                Heading { text: "Lienzo" }
                PropRow {
                    label: "Rejilla"
                    Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        on: App.editor.grid
                        onToggled: on => App.editor.grid = on
                    }
                }
                PropRow {
                    label: "Imán"
                    Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        on: App.editor.snap
                        onToggled: on => App.editor.snap = on
                    }
                }
                Rule {}
                Heading { text: "Colores del dibujo" }
                Grid {
                    columns: 8
                    spacing: 5
                    Repeater {
                        model: App.editor.palette
                        Rectangle {
                            required property string modelData
                            width: 22
                            height: 22
                            radius: 6
                            color: modelData
                            border.color: Theme.dark ? "#30ffffff" : "#24000000"
                            Sym {
                                anchors.centerIn: parent
                                visible: swatchHit.containsMouse
                                name: "x"
                                size: 11
                                color: Qt.color(parent.modelData).hslLightness > 0.6 ? "#000000" : "#ffffff"
                            }
                            MouseArea {
                                id: swatchHit
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: App.editor.removePaletteColor(parent.modelData)
                            }
                        }
                    }
                }
                PropRow {
                    label: "Añadir"
                    ColorWell {
                        anchors.verticalCenter: parent.verticalCenter
                        value: Theme.tint
                        onPicked: color => App.editor.addPaletteColor(color)
                    }
                }
                Rule {}
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    lineHeight: 1.25
                    text: "Selecciona un objeto para darle formato. Lo que elijas sin nada seleccionado no cambia el dibujo."
                    color: Theme.faint
                    font.pixelSize: 12
                }
            }

            // --- Something selected ---
            Column {
                visible: panel.some
                width: parent.width
                spacing: 4

                Heading { text: "Posición" }
                PropRow {
                    label: "X · Y"
                    NumField {
                        value: panel.info.x ?? 0
                        onEdited: value => panel.set("x", value)
                    }
                    NumField {
                        value: panel.info.y ?? 0
                        onEdited: value => panel.set("y", value)
                    }
                }
                PropRow {
                    label: "Tamaño"
                    visible: !(panel.info.allLines ?? false)
                    NumField {
                        value: panel.info.w ?? 0
                        from: 1
                        onEdited: value => panel.set("w", value)
                    }
                    NumField {
                        value: panel.info.h ?? 0
                        from: 1
                        onEdited: value => panel.set("h", value)
                    }
                }
                PropRow {
                    label: "Giro"
                    visible: !(panel.info.allLines ?? false) && !(panel.info.isFrame ?? false)
                    NumField {
                        value: panel.info.rotation ?? 0
                        unit: "°"
                        from: -360
                        to: 360
                        step: 15
                        onEdited: value => panel.set("rotation", value)
                    }
                }
                PropRow {
                    label: "Esquinas"
                    visible: panel.info.hasRadius ?? false
                    NumField {
                        value: panel.info.radius ?? 0
                        from: 0
                        to: 500
                        step: 2
                        onEdited: value => panel.set("radius", value)
                    }
                }
                PropRow {
                    label: "Lados"
                    visible: panel.info.hasSides ?? false
                    NumField {
                        value: panel.info.sides ?? 5
                        decimals: 0
                        from: 3
                        to: 24
                        onEdited: value => panel.set("sides", value)
                    }
                }
                // Wider than a labelled row has room for: these take the whole line.
                Row {
                    visible: (panel.info.units ?? 0) > 1
                    height: 30
                        Mini { symbol: "align-start-vertical"; onClicked: panel.run("align-left") }
                        Mini { symbol: "align-center-vertical"; onClicked: panel.run("align-hcenter") }
                        Mini { symbol: "align-end-vertical"; onClicked: panel.run("align-right") }
                        Mini { symbol: "align-start-horizontal"; onClicked: panel.run("align-top") }
                        Mini { symbol: "align-center-horizontal"; onClicked: panel.run("align-vcenter") }
                        Mini { symbol: "align-end-horizontal"; onClicked: panel.run("align-bottom") }
                        Item { width: 8; height: 1 }
                        Mini { symbol: "align-horizontal-distribute-center"; enabled: (panel.info.units ?? 0) > 2; onClicked: panel.run("distribute-h") }
                        Mini { symbol: "align-vertical-distribute-center"; enabled: (panel.info.units ?? 0) > 2; onClicked: panel.run("distribute-v") }
                }

                Rule { visible: fillBlock.visible }
                Column {
                    id: fillBlock
                    visible: panel.info.hasFill ?? false
                    width: parent.width
                    spacing: 4
                    Heading { text: "Relleno" }
                    Pick {
                        width: parent.width
                        value: panel.info.fillKind ?? "solid"
                        mixed: panel.mixed("fillKind")
                        options: [{key: "none", label: "Sin relleno"}, {key: "solid", label: "Color"},
                                  {key: "linear", label: "Degradado lineal"}, {key: "radial", label: "Degradado radial"}]
                        onChosen: key => panel.set("fillKind", key)
                    }
                    PropRow {
                        label: (panel.info.fillKind ?? "") === "solid" ? "Color" : "Colores"
                        visible: (panel.info.fillKind ?? "none") !== "none"
                        ColorWell {
                            anchors.verticalCenter: parent.verticalCenter
                            value: panel.info.fill ?? "#ffffff"
                            mixed: panel.mixed("fill")
                            onPicked: color => panel.set("fill", color)
                        }
                        ColorWell {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: (panel.info.fillKind ?? "") === "linear" || (panel.info.fillKind ?? "") === "radial"
                            value: panel.info.fill2 ?? "#ffffff"
                            onPicked: color => panel.set("fill2", color)
                        }
                    }
                    PropRow {
                        label: "Ángulo"
                        visible: (panel.info.fillKind ?? "") === "linear"
                        NumField {
                            value: panel.info.gradientAngle ?? 90
                            unit: "°"
                            from: 0
                            to: 360
                            step: 15
                            onEdited: value => panel.set("gradientAngle", value)
                        }
                    }
                }

                Rule { visible: strokeBlock.visible }
                Column {
                    id: strokeBlock
                    visible: !(panel.info.isImage ?? false)
                    width: parent.width
                    spacing: 4
                    Heading { text: (panel.info.allLines ?? false) ? "Línea" : "Trazo" }
                    PropRow {
                        label: "Color"
                        ColorWell {
                            anchors.verticalCenter: parent.verticalCenter
                            value: panel.info.stroke ?? "#000000"
                            mixed: panel.mixed("stroke")
                            onPicked: color => panel.set("stroke", color)
                        }
                        NumField {
                            value: panel.info.strokeWidth ?? 0
                            mixed: panel.mixed("strokeWidth")
                            from: 0
                            to: 80
                            step: 0.5
                            unit: "px"
                            onEdited: value => panel.set("strokeWidth", value)
                        }
                    }
                    PropRow {
                        label: "Estilo"
                        Pick {
                            value: panel.info.dash ?? "solid"
                            mixed: panel.mixed("dash")
                            options: [{key: "solid", label: "Continuo"}, {key: "dash", label: "Guiones"},
                                      {key: "dot", label: "Puntos"}, {key: "dashdot", label: "Guion y punto"}]
                            onChosen: key => panel.set("dash", key)
                        }
                    }
                    PropRow {
                        label: "Recorrido"
                        visible: panel.info.kind === "line"
                        Pick {
                            value: panel.info.route ?? "straight"
                            options: [{key: "straight", label: "Recto"}, {key: "elbow", label: "En codo"}, {key: "curve", label: "Curvo"}]
                            onChosen: key => panel.set("route", key)
                        }
                    }
                    PropRow {
                        label: "Inicio"
                        visible: panel.info.hasLine ?? false
                        Pick {
                            value: panel.info.startArrow ?? "none"
                            mixed: panel.mixed("startArrow")
                            options: panel.heads
                            onChosen: key => panel.set("startArrow", key)
                        }
                    }
                    PropRow {
                        label: "Final"
                        visible: panel.info.hasLine ?? false
                        Pick {
                            value: panel.info.endArrow ?? "none"
                            mixed: panel.mixed("endArrow")
                            options: panel.heads
                            onChosen: key => panel.set("endArrow", key)
                        }
                    }
                }

                Rule {}
                Heading { text: "Aspecto" }
                PropRow {
                    label: "Opacidad"
                    NumField {
                        value: Math.round((panel.info.opacity ?? 1) * 100)
                        mixed: panel.mixed("opacity")
                        decimals: 0
                        unit: "%"
                        from: 0
                        to: 100
                        step: 5
                        onEdited: value => panel.set("opacity", value / 100)
                    }
                }
                PropRow {
                    label: "Sombra"
                    visible: !(panel.info.allLines ?? false)
                    Toggle {
                        anchors.verticalCenter: parent.verticalCenter
                        on: panel.info.shadow ?? false
                        onToggled: on => panel.set("shadow", on)
                    }
                }

                Rule { visible: textBlock.visible }
                Column {
                    id: textBlock
                    visible: panel.info.hasText ?? false
                    width: parent.width
                    spacing: 4
                    Heading { text: "Texto" }
                    FCombo {
                        id: family
                        readonly property var families: ["Predeterminada"].concat(Qt.fontFamilies())
                        width: parent.width
                        model: families
                        currentIndex: Math.max(0, families.indexOf(panel.info.fontFamily ?? ""))
                        onActivated: index => panel.set("fontFamily", index === 0 ? "" : families[index])
                    }
                    PropRow {
                        label: "Tamaño"
                        NumField {
                            value: panel.info.fontSize ?? 16
                            mixed: panel.mixed("fontSize")
                            decimals: 0
                            from: 4
                            to: 400
                            onEdited: value => panel.set("fontSize", value)
                        }
                        ColorWell {
                            anchors.verticalCenter: parent.verticalCenter
                            value: panel.info.textColor ?? "#000000"
                            mixed: panel.mixed("textColor")
                            onPicked: color => panel.set("textColor", color)
                        }
                    }
                    PropRow {
                        label: "Estilo"
                        Row {
                            Mini { symbol: "bold"; checked: panel.info.bold ?? false; onClicked: panel.set("bold", !checked) }
                            Mini { symbol: "italic"; checked: panel.info.italic ?? false; onClicked: panel.set("italic", !checked) }
                            Mini { symbol: "underline"; checked: panel.info.underline ?? false; onClicked: panel.set("underline", !checked) }
                        }
                    }
                    PropRow {
                        label: "Alinear"
                        Row {
                            Mini { symbol: "text-align-start"; checked: panel.info.hAlign === "left"; onClicked: panel.set("hAlign", "left") }
                            Mini { symbol: "text-align-center"; checked: panel.info.hAlign === "center"; onClicked: panel.set("hAlign", "center") }
                            Mini { symbol: "text-align-end"; checked: panel.info.hAlign === "right"; onClicked: panel.set("hAlign", "right") }
                        }
                    }
                    PropRow {
                        label: "Vertical"
                        visible: panel.info.kind !== "text" && panel.info.kind !== "line"
                        Pick {
                            value: panel.info.vAlign ?? "middle"
                            options: [{key: "top", label: "Arriba"}, {key: "middle", label: "Centro"}, {key: "bottom", label: "Abajo"}]
                            onChosen: key => panel.set("vAlign", key)
                        }
                    }
                }
            }
        }
    }

    readonly property var heads: [{key: "none", label: "Sin punta"}, {key: "arrow", label: "Flecha"}, {key: "triangle", label: "Triángulo"},
                                  {key: "circle", label: "Círculo"}, {key: "diamond", label: "Rombo"}, {key: "bar", label: "Barra"}]
}
