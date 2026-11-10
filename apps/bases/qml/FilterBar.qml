import QtQuick
import QtQuick.Controls.Basic

/// The filters of a table as a chain of pills: column · operator · value.
/// Every one must hold; the model binds the values.
Item {
    id: chain

    /// The GridModel filtered.
    property var model
    readonly property var conditions: model ? model.conditions : []
    readonly property var operators: model ? model.operators() : []
    readonly property bool adding: editor.visible
    signal changed()

    function label(op) {
        const found = operators.find(o => o.id === op)
        return found ? found.label : op
    }
    function unary(op) {
        const found = operators.find(o => o.id === op)
        return !!found && found.unary
    }
    function add(column, op, value) {
        if (!model.fields.some(f => f.name === column) || !operators.some(o => o.id === op))
            return false
        const next = conditions.slice()
        next.push({column: column, op: op, value: unary(op) ? "" : String(value ?? "")})
        model.conditions = next
        changed()
        return true
    }
    function removeAt(index) {
        const next = conditions.slice()
        next.splice(index, 1)
        model.conditions = next
        changed()
    }
    function clear() {
        if (conditions.length === 0)
            return
        model.conditions = []
        changed()
    }
    /// Opens the editor of a new pill, on `column` when given.
    function compose(column) {
        const at = model.fields.findIndex(f => f.name === column)
        columnBox.currentIndex = Math.max(0, at)
        opBox.currentIndex = 0
        valueField.text = ""
        editor.open()
        valueField.forceActiveFocus()
    }

    implicitHeight: 36

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        contentWidth: row.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: row
            height: parent.height
            spacing: 6
            Sym {
                anchors.verticalCenter: parent.verticalCenter
                name: "list-filter"
                size: 14
                color: Theme.dim
            }
            Repeater {
                model: chain.conditions
                Rectangle {
                    id: pill
                    required property var modelData
                    required property int index
                    anchors.verticalCenter: parent.verticalCenter
                    width: words.width + 34
                    height: 24
                    radius: 12
                    color: Theme.selection
                    border.color: Qt.alpha(Theme.tint, 0.35)
                    Row {
                        id: words
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Text {
                            text: pill.modelData.column
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: chain.label(pill.modelData.op)
                            color: Theme.dim
                            font.pixelSize: 12
                        }
                        Text {
                            visible: !chain.unary(pill.modelData.op)
                            width: Math.min(implicitWidth, 180)
                            text: pill.modelData.value === "" ? "«»" : pill.modelData.value
                            color: Theme.tint
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    TButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        symbol: "x"
                        size: 11
                        tone: Theme.dim
                        onClicked: chain.removeAt(pill.index)
                    }
                }
            }
            Rectangle {
                id: more
                anchors.verticalCenter: parent.verticalCenter
                width: moreText.implicitWidth + 34
                height: 24
                radius: 12
                color: moreArea.containsMouse ? Theme.sideSelection : Theme.field
                Sym {
                    x: 9
                    anchors.verticalCenter: parent.verticalCenter
                    name: "plus"
                    size: 12
                    color: Theme.dim
                }
                Text {
                    id: moreText
                    x: 25
                    anchors.verticalCenter: parent.verticalCenter
                    text: chain.conditions.length === 0 ? "Añadir filtro" : "Y además…"
                    color: Theme.dim
                    font.pixelSize: 12
                }
                MouseArea {
                    id: moreArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: chain.compose("")
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: chain.conditions.length > 0
                text: "Quitar todos"
                color: clearArea.containsMouse ? Theme.text : Theme.dim
                font.pixelSize: 12
                leftPadding: 4
                MouseArea {
                    id: clearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: chain.clear()
                }
            }
        }
    }

    Popup {
        id: editor
        x: 12
        y: chain.height + 2
        width: 468
        height: 98
        padding: 12
        background: Rectangle {
            radius: 10
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: Item {
            Row {
                spacing: 8
                FCombo {
                    id: columnBox
                    width: 150
                    model: chain.model ? chain.model.fields.map(f => f.name) : []
                }
                FCombo {
                    id: opBox
                    width: 150
                    model: chain.operators.map(o => o.label)
                }
                Field {
                    id: valueField
                    width: 128
                    enabled: !chain.operators[Math.max(0, opBox.currentIndex)]?.unary
                    opacity: enabled ? 1 : 0.4
                    placeholderText: "valor"
                    onAccepted: accept.clicked()
                }
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                width: parent.width - 200
                text: "Los filtros se suman: todos deben cumplirse."
                color: Theme.faint
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8
                DialogButton {
                    text: "Cancelar"
                    onClicked: editor.close()
                }
                DialogButton {
                    id: accept
                    text: "Filtrar"
                    primary: true
                    onClicked: {
                        if (chain.add(columnBox.currentText, chain.operators[opBox.currentIndex].id, valueField.text))
                            editor.close()
                    }
                }
            }
        }
    }
}
