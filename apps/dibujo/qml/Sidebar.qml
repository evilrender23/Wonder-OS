import QtQuick
import QtQuick.Controls.Basic

/// The left of the window: the frames and the layers of the drawing, and
/// the library of forms.
Rectangle {
    id: side
    /// layers or shapes.
    property string tab: "layers"
    signal frameMenu(Item anchor)
    color: Theme.sidebar

    Segmented {
        id: tabs
        x: 12
        y: 10
        options: [{key: "layers", label: "Capas"}, {key: "shapes", label: "Formas"}]
        current: side.tab
        onPicked: key => side.tab = key
    }

    // --- Frames and layers ---
    Flickable {
        id: layersView
        visible: side.tab === "layers"
        /// Scrolls just enough to show a row of the list.
        function show(row) {
            if (!row || !visible)
                return
            const top = row.mapToItem(layersColumn, 0, 0).y + layersColumn.y
            if (top < contentY)
                contentY = Math.max(0, top - 6)
            else if (top + row.height > contentY + height)
                contentY = Math.min(Math.max(0, contentHeight - height), top + row.height - height + 6)
        }
        x: 0
        y: 48
        width: parent.width
        height: parent.height - y
        contentHeight: layersColumn.height + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: layersColumn
            x: 8
            width: parent.width - 16
            Item {
                width: parent.width
                height: 28
                Text {
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "MARCOS"
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                TButton {
                    id: addFrame
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 22
                    symbol: "plus"
                    size: 14
                    tone: Theme.dim
                    onClicked: side.frameMenu(addFrame)
                }
            }
            Text {
                visible: App.editor.frames.length === 0
                x: 6
                width: parent.width - 12
                height: visible ? contentHeight + 10 : 0
                wrapMode: Text.Wrap
                text: "Un marco es una página con nombre: lo que se exporta."
                color: Theme.faint
                font.pixelSize: 12
            }
            Repeater {
                model: App.editor.frames
                Rectangle {
                    id: frameRow
                    required property var modelData
                    readonly property bool on: App.editor.selection.includes(modelData.id)
                    width: parent.width
                    height: 28
                    radius: 6
                    color: on ? Theme.sideSelection : (frameHit.containsMouse ? Theme.hover : "transparent")
                    MouseArea {
                        id: frameHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            App.editor.select(frameRow.modelData.id)
                            App.editor.reveal(frameRow.modelData.id)
                        }
                    }
                    Sym {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: "proportions"
                        size: 14
                        color: frameRow.on ? Theme.tint : Theme.dim
                    }
                    Text {
                        x: 30
                        width: parent.width - 38 - frameSize.implicitWidth - 8
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: frameRow.modelData.name
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    Text {
                        id: frameSize
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: frameRow.modelData.size
                        color: Theme.faint
                        font.pixelSize: 11
                    }
                }
            }
            Item {
                width: parent.width
                height: 34
                Text {
                    x: 6
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    text: "CAPAS"
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    text: App.editor.count
                    color: Theme.faint
                    font.pixelSize: 11
                }
            }
            Text {
                visible: App.editor.count === 0
                x: 6
                width: parent.width - 12
                wrapMode: Text.Wrap
                text: "El lienzo está vacío. Elige una herramienta abajo o una forma de la biblioteca."
                color: Theme.faint
                font.pixelSize: 12
            }
            Repeater {
                model: App.editor.layers
                Rectangle {
                    id: layer
                    required property var modelData
                    readonly property bool on: App.editor.selection.includes(modelData.id)
                    readonly property bool last: on && App.editor.selection[App.editor.selection.length - 1] === modelData.id
                    onLastChanged: if (last) Qt.callLater(layersView.show, layer)
                    Component.onCompleted: if (last) Qt.callLater(layersView.show, layer)
                    width: parent.width
                    height: 28
                    radius: 6
                    color: on ? Theme.sideSelection : (layerHit.containsMouse ? Theme.hover : "transparent")
                    MouseArea {
                        id: layerHit
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: mouse => App.editor.select(layer.modelData.id, (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
                        onDoubleClicked: {
                            rename.text = layer.modelData.name
                            rename.visible = true
                            rename.forceActiveFocus()
                            rename.selectAll()
                        }
                    }
                    Sym {
                        x: layer.modelData.grouped ? 16 : 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: layer.modelData.symbol
                        size: 14
                        color: layer.on ? Theme.tint : Theme.dim
                        opacity: layer.modelData.hidden ? 0.4 : 1
                    }
                    Text {
                        x: layer.modelData.grouped ? 38 : 30
                        width: parent.width - x - 52
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !rename.visible
                        elide: Text.ElideRight
                        text: layer.modelData.name
                        color: layer.modelData.hidden ? Theme.faint : Theme.text
                        font.pixelSize: 13
                    }
                    Field {
                        id: rename
                        visible: false
                        x: 26
                        width: parent.width - x - 4
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        onEditingFinished: {
                            if (visible && text.trim() !== "")
                                App.editor.rename(layer.modelData.id, text.trim())
                            visible = false
                        }
                        Keys.onEscapePressed: visible = false
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !rename.visible
                        TButton {
                            width: 24
                            height: 22
                            size: 13
                            tone: Theme.dim
                            symbol: layer.modelData.locked ? "lock" : "lock-open"
                            opacity: layer.modelData.locked || layerHit.containsMouse ? 1 : 0
                            onClicked: App.editor.setLayerLocked(layer.modelData.id, !layer.modelData.locked)
                        }
                        TButton {
                            width: 24
                            height: 22
                            size: 13
                            tone: Theme.dim
                            symbol: layer.modelData.hidden ? "eye-off" : "eye"
                            opacity: layer.modelData.hidden || layerHit.containsMouse ? 1 : 0
                            onClicked: App.editor.setLayerHidden(layer.modelData.id, !layer.modelData.hidden)
                        }
                    }
                }
            }
        }
    }

    // --- The library of forms ---
    Flickable {
        visible: side.tab === "shapes"
        y: 48
        width: parent.width
        height: parent.height - y
        contentHeight: library.height + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: library
            x: 12
            width: parent.width - 24
            spacing: 2
            Repeater {
                model: App.formSections()
                Column {
                    id: group
                    required property var modelData
                    width: parent.width
                    Item {
                        width: parent.width
                        height: 30
                        Text {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 7
                            text: group.modelData.section.toUpperCase()
                            color: Theme.dim
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                    Grid {
                        id: tiles
                        columns: 4
                        spacing: 6
                        readonly property real cell: Math.floor((group.width - spacing * 3) / 4)
                        Repeater {
                            model: group.modelData.forms
                            Rectangle {
                                id: tile
                                required property var modelData
                                width: tiles.cell
                                height: tiles.cell
                                radius: 8
                                color: tileHit.containsMouse ? Theme.sideSelection : Theme.field
                                Image {
                                    anchors.centerIn: parent
                                    width: parent.width - 14
                                    height: width
                                    sourceSize: Qt.size(width * 2, height * 2)
                                    source: "image://thumb/form/" + tile.modelData.form + "/" + tile.modelData.sides + "/" + tile.modelData.radius
                                        + "/" + String(tileHit.containsMouse ? Theme.tint : Theme.text).slice(1)
                                }
                                MouseArea {
                                    id: tileHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: App.editor.addForm(tile.modelData.form, tile.modelData.sides, tile.modelData.radius)
                                }
                                ToolTip.visible: tileHit.containsMouse
                                ToolTip.delay: 600
                                ToolTip.text: tile.modelData.name
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: 8 }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Pulsa una forma para ponerla en el centro de la vista."
                color: Theme.faint
                font.pixelSize: 11
            }
        }
    }
}
