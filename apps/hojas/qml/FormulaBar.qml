import QtQuick
import QtQuick.Controls.Basic

/// Where the cell is and what it holds; under it, while a formula is typed,
/// the functions that fit and what the one in hand expects.
Rectangle {
    id: bar
    required property var host
    readonly property var book: host.book
    readonly property bool helping: host.inFormula && (host.completions.length > 0 || host.formulaHelp.name !== undefined)
    implicitHeight: 36 + (helping ? 30 : 0)
    color: Theme.window
    clip: true

    function takeFocus() {
        input.forceActiveFocus()
    }
    function focusName() {
        nameBox.forceActiveFocus()
        nameBox.selectAll()
    }

    Field {
        id: nameBox
        x: 8
        y: 4
        width: 92
        height: 28
        horizontalAlignment: TextInput.AlignHCenter
        font.pixelSize: 12
        text: bar.host.chartId !== 0 ? "Gráfico" : bar.host.selectionName()
        onAccepted: {
            bar.host.goTo(text)
            text = Qt.binding(() => bar.host.chartId !== 0 ? "Gráfico" : bar.host.selectionName())
        }
        Keys.onEscapePressed: {
            text = Qt.binding(() => bar.host.chartId !== 0 ? "Gráfico" : bar.host.selectionName())
            bar.host.cancelEdit()
            bar.host.select(bar.host.curRow, bar.host.curCol, false)
        }
        onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => bar.host.chartId !== 0 ? "Gráfico" : bar.host.selectionName())
    }
    Text {
        id: fx
        x: nameBox.x + nameBox.width + 12
        y: 4
        height: 28
        verticalAlignment: Text.AlignVCenter
        text: "fx"
        color: bar.host.inFormula || bar.host.info.formula === true ? Theme.tint : Theme.faint
        font.pixelSize: 13
        font.italic: true
        font.weight: Font.DemiBold
    }
    Field {
        id: input
        x: fx.x + fx.width + 10
        y: 4
        width: parent.width - x - 8
        height: 28
        readonly property bool painted: bar.host.inFormula && contentWidth <= width - 18
        color: painted ? "transparent" : Theme.text
        placeholderText: bar.host.chartId !== 0 ? "" : "Escribe un valor o una fórmula que empiece por ="
        background: Rectangle {
            radius: 7
            color: input.activeFocus ? Theme.field : "transparent"
            border.color: input.activeFocus ? Theme.tint : "transparent"
        }
        // What the cell holds, until it is being typed.
        readonly property string shown: { bar.book.revision; return bar.host.editing ? bar.host.editText : (bar.book.open && bar.host.chartId === 0 ? bar.book.input(bar.host.curRow, bar.host.curCol) : "") }
        onShownChanged: if (text !== shown) text = shown
        Component.onCompleted: text = shown
        onActiveFocusChanged: {
            if (activeFocus && !bar.host.editing) {
                bar.host.beginEdit(undefined, "bar", false)
            } else if (activeFocus) {
                bar.host.editPlace = "bar"
                bar.host.editCursor = cursorPosition
            }
        }
        onTextEdited: {
            bar.host.pointStart = -1
            if (bar.host.editing)
                bar.host.editText = text
        }
        onCursorPositionChanged: if (activeFocus) bar.host.editCursor = cursorPosition
        Connections {
            target: bar.host
            function onCursorWanted(position) {
                if (bar.host.editPlace === "bar") {
                    input.text = bar.host.editText
                    input.cursorPosition = position
                }
            }
        }
        Keys.onPressed: event => {
            const shift = (event.modifiers & Qt.ShiftModifier) !== 0
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                bar.host.commit(event.modifiers & Qt.ControlModifier ? 1 : (shift ? -1 : 1), 0, (event.modifiers & Qt.ControlModifier) !== 0)
                event.accepted = true
            } else if (event.key === Qt.Key_Tab) {
                if (!bar.host.acceptCompletion(0))
                    bar.host.commit(0, 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                bar.host.cancelEdit()
                event.accepted = true
            }
        }
        Text {
            visible: input.painted
            x: input.leftPadding
            width: input.width - input.leftPadding - input.rightPadding
            height: input.height
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.StyledText
            font: input.font
            color: Theme.text
            text: Colors.formula(bar.host.editText, bar.host.references, Theme.text)
        }
    }

    // The help.
    Item {
        visible: bar.helping
        x: input.x
        y: 36
        width: input.width
        height: 26
        // The functions a few letters may mean.
        Row {
            visible: bar.host.completions.length > 0
            spacing: 6
            Repeater {
                model: bar.host.completions.slice(0, 7)
                Rectangle {
                    required property var modelData
                    required property int index
                    width: chip.implicitWidth + 16
                    height: 24
                    radius: 6
                    color: index === 0 ? Qt.alpha(Theme.tint, Theme.dark ? 0.28 : 0.15) : (chipArea.containsMouse ? Theme.sideSelection : Theme.field)
                    Row {
                        id: chip
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: text !== ""
                            anchors.baseline: parent.children[0].baseline
                            text: modelData.other
                            color: Theme.dim
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: chipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: bar.host.acceptCompletion(parent.index)
                    }
                }
            }
            Text {
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: "Tab para completar"
                color: Theme.faint
                font.pixelSize: 11
            }
        }
        // What the function in hand expects, the argument being typed in the tint.
        Row {
            visible: bar.host.completions.length === 0 && bar.host.formulaHelp.name !== undefined
            height: 24
            spacing: 0
            Text {
                height: 24
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                color: Theme.text
                font.pixelSize: 12
                text: {
                    const help = bar.host.formulaHelp
                    if (help.name === undefined)
                        return ""
                    const parts = help.parts === undefined ? [] : help.parts
                    let out = "<b>" + help.name + "</b>("
                    for (let i = 0; i < parts.length; ++i) {
                        // The last one stands for the rest when it repeats.
                        const on = i === help.argument || (i === parts.length - 1 && help.argument >= parts.length && parts[i].includes("…"))
                        out += (i > 0 ? "; " : "") + (on ? "<font color=\"" + Theme.tint + "\"><b>" + Colors.html(parts[i]) + "</b></font>" : Colors.html(parts[i]))
                    }
                    return out + ")"
                }
            }
            Text {
                height: 24
                leftPadding: 12
                width: Math.max(0, parent.parent.width - x)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: bar.host.formulaHelp.help === undefined ? "" : bar.host.formulaHelp.help
                color: Theme.dim
                font.pixelSize: 12
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }
}
