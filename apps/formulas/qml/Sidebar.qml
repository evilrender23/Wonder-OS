import QtQuick
import QtQuick.Controls.Basic

/// The left of the window: the equations of the document, the symbols and
/// the templates to write with, and the formulas kept to use again.
Rectangle {
    id: side
    /// equations, symbols or library.
    property string tab: "equations"
    property alias query: search.text
    /// What the pointer is over, said at the foot.
    property string hint
    signal insertRequested(string snippet)
    signal useRequested(string latex)
    signal saveRequested()
    color: Theme.sidebar

    function searchFor(text) {
        search.text = text
    }
    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }

    Segmented {
        id: tabs
        x: 12
        y: 10
        width: parent.width - 24
        options: [{key: "equations", label: "Ecuaciones"}, {key: "symbols", label: "Símbolos"}, {key: "library", label: "Biblioteca"}]
        current: side.tab
        onPicked: key => side.tab = key
    }
    Field {
        id: search
        visible: side.tab !== "equations"
        x: 12
        y: 48
        width: parent.width - 24 - (side.tab === "library" ? 34 : 0)
        leftPadding: 30
        placeholderText: side.tab === "symbols" ? "integral, flecha, alfa…" : "Buscar en la biblioteca"
        Sym {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            name: "search"
            size: 14
            color: Theme.dim
        }
    }
    TButton {
        visible: side.tab === "library"
        x: parent.width - 12 - width
        y: 49
        symbol: "plus"
        onClicked: side.saveRequested()
    }

    // --- The equations of the document ---
    Item {
        visible: side.tab === "equations"
        y: 44
        width: parent.width
        height: parent.height - y
        Text {
            x: 16
            y: 8
            text: App.equations.length === 1 ? "1 ecuación" : App.equations.length + " ecuaciones"
            color: Theme.dim
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        TButton {
            x: parent.width - 12 - width
            y: 1
            symbol: "plus"
            onClicked: App.addEquation("")
        }
        ListView {
            id: equations
            x: 8
            y: 32
            width: parent.width - 16
            height: parent.height - y - 8
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            model: App.equations
            currentIndex: App.current
            highlightMoveDuration: 0
            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                readonly property bool on: App.current === index
                width: equations.width
                height: 58
                radius: 9
                color: on ? Theme.sideSelection : (rowHit.containsMouse ? Theme.hover : "transparent")
                MouseArea {
                    id: rowHit
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: App.current = row.index
                }
                Rectangle {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    radius: 11
                    color: row.on ? Theme.tint : "transparent"
                    border.color: row.on ? "transparent" : Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: row.index + 1
                        color: row.on ? "#ffffff" : Theme.dim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
                Thumb {
                    x: 38
                    y: 5
                    width: parent.width - 46 - (tools.visible ? tools.width : 0)
                    height: parent.height - 10
                    latex: row.modelData
                    size: 15
                }
                Text {
                    visible: row.modelData.trim() === ""
                    x: 40
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Vacía"
                    color: Theme.faint
                    font.pixelSize: 12
                }
                Column {
                    id: tools
                    visible: rowHit.containsMouse || upHit.hovered || downHit.hovered
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    TButton {
                        width: 24
                        height: 22
                        size: 13
                        symbol: "chevron-up"
                        active: row.index > 0
                        onClicked: App.moveEquation(row.index, row.index - 1)
                        HoverHandler { id: upHit }
                    }
                    TButton {
                        width: 24
                        height: 22
                        size: 13
                        symbol: "chevron-down"
                        active: row.index < App.equations.length - 1
                        onClicked: App.moveEquation(row.index, row.index + 1)
                        HoverHandler { id: downHit }
                    }
                }
            }
        }
    }

    // --- Symbols and templates ---
    Flickable {
        visible: side.tab === "symbols"
        y: 84
        width: parent.width
        height: parent.height - y - foot.height
        contentHeight: groups.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: groups
            x: 12
            width: parent.width - 24
            Repeater {
                model: side.tab === "symbols" ? App.palette(search.text) : []
                Column {
                    id: group
                    required property var modelData
                    /// Every cell of a group is as tall as its tallest, so that the rows read as rows.
                    property real tallest: 32
                    width: groups.width
                    Heading { text: group.modelData.title }
                    Flow {
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: group.modelData.items
                            Rectangle {
                                id: cell
                                required property var modelData
                                width: Math.max(34, Math.min(groups.width, glyph.implicitWidth + 10))
                                readonly property real wanted: Math.min(64, glyph.implicitHeight + 6)
                                onWantedChanged: group.tallest = Math.max(group.tallest, wanted)
                                height: group.tallest
                                radius: 6
                                color: cellHit.containsMouse ? Theme.hover : "transparent"
                                FormulaItem {
                                    id: glyph
                                    anchors.centerIn: parent
                                    latex: cell.modelData.latex
                                    size: 16
                                    fit: groups.width - 10
                                    color: Theme.text
                                    scale: implicitHeight > 58 ? 58 / implicitHeight : 1
                                }
                                MouseArea {
                                    id: cellHit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onEntered: side.hint = cell.modelData.title + "   " + cell.modelData.insert.replace("|", "")
                                    onExited: side.hint = ""
                                    onClicked: side.insertRequested(cell.modelData.insert)
                                }
                            }
                        }
                    }
                    Item { width: 1; height: 6 }
                }
            }
            Text {
                visible: side.tab === "symbols" && search.text.trim() !== "" && groups.children.length <= 2
                topPadding: 14
                width: parent.width
                wrapMode: Text.Wrap
                text: "Ningún símbolo se llama así."
                color: Theme.dim
                font.pixelSize: 12
            }
        }
    }
    Rectangle {
        id: foot
        visible: side.tab === "symbols"
        y: parent.height - height
        width: parent.width
        height: 28
        color: "transparent"
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }
        Text {
            x: 14
            width: parent.width - 28
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: side.hint === "" ? "Pulsa un símbolo para escribirlo" : side.hint
            color: side.hint === "" ? Theme.faint : Theme.dim
            font.pixelSize: 11
        }
    }

    // --- The library ---
    Flickable {
        visible: side.tab === "library"
        y: 84
        width: parent.width
        height: parent.height - y
        contentHeight: shelves.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: shelves
            x: 12
            width: parent.width - 24
            Repeater {
                model: {
                    App.libraryRevision
                    return side.tab === "library" ? App.library(search.text) : []
                }
                Column {
                    id: shelf
                    required property var modelData
                    width: shelves.width
                    spacing: 6
                    Heading { text: shelf.modelData.title }
                    Text {
                        visible: shelf.modelData.own && shelf.modelData.entries.length === 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Guarda aquí la fórmula que tienes delante con el botón + o con Ctrl B."
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: shelf.modelData.entries
                        Rectangle {
                            id: entry
                            required property var modelData
                            width: shelf.width
                            height: 78
                            radius: 9
                            color: Theme.dark ? "#14ffffff" : "#b0ffffff"
                            border.color: entryHit.containsMouse ? Theme.tint : Theme.line
                            MouseArea {
                                id: entryHit
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: side.useRequested(entry.modelData.latex)
                            }
                            Text {
                                x: 10
                                y: 7
                                width: parent.width - 20 - (drop.visible ? 22 : 0)
                                elide: Text.ElideRight
                                text: entry.modelData.name
                                color: Theme.dim
                                font.pixelSize: 11
                            }
                            Thumb {
                                x: 8
                                y: 24
                                width: parent.width - 16
                                height: parent.height - 30
                                latex: entry.modelData.latex
                                size: 15
                            }
                            TButton {
                                id: drop
                                visible: entry.modelData.own && (entryHit.containsMouse || dropHit.hovered)
                                anchors.right: parent.right
                                anchors.rightMargin: 3
                                y: 2
                                width: 22
                                height: 22
                                size: 12
                                symbol: "x"
                                tone: Theme.dim
                                onClicked: App.removeFromLibrary(entry.modelData.id)
                                HoverHandler { id: dropHit }
                            }
                        }
                    }
                    Item { width: 1; height: 4 }
                }
            }
        }
    }
}
