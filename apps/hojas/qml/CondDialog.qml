import QtQuick
import QtQuick.Controls.Basic

/// A rule of conditional format for the selection.
Popup {
    id: dialog
    required property var host
    readonly property var ops: [">", "<", ">=", "<=", "=", "<>", "between", "contains", "duplicate", "top", "bottom"]
    readonly property var looks: [
        {name: "Rojo", fill: "#fde2e1", color: "#b3261e"},
        {name: "Ámbar", fill: "#fff4c2", color: "#8a5a00"},
        {name: "Verde", fill: "#dcf5e3", color: "#17663a"},
        {name: "Azul", fill: "#dbeaff", color: "#0b4fa8"},
        {name: "Violeta", fill: "#e6e3ff", color: "#4338a8"},
    ]
    property int look: 0
    readonly property string op: ops[opBox.currentIndex]
    readonly property bool needsValue: op !== "duplicate"

    function show() {
        first.text = ""
        second.text = ""
        open()
        first.forceActiveFocus()
    }
    function accept() {
        if (needsValue && first.text.trim() === "")
            return
        const s = host.selection
        host.book.addConditional(s.r1, s.c1, s.r2, s.c2, op, first.text.trim(), second.text.trim(), looks[look].fill, looks[look].color, bold.on)
        close()
    }

    modal: true
    anchors.centerIn: parent
    width: 380
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 12; color: Theme.menu; border.color: Theme.border }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Column {
        spacing: 11
        Text {
            text: "Formato condicional"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            text: "Resalta en " + dialog.host.selectionName() + " las celdas cuyo valor es"
            color: Theme.dim
            font.pixelSize: 13
        }
        FCombo {
            id: opBox
            width: parent.width
            model: ["mayor que", "menor que", "mayor o igual que", "menor o igual que", "igual a", "distinto de", "entre dos valores", "un texto que contiene", "un valor duplicado", "de los mayores (cuántos)", "de los menores (cuántos)"]
        }
        Row {
            visible: dialog.needsValue
            spacing: 8
            Field {
                id: first
                width: dialog.op === "between" ? 152 : 344
                placeholderText: dialog.op === "contains" ? "Texto" : (dialog.op === "top" || dialog.op === "bottom" ? "Cuántos" : "Valor")
                onAccepted: dialog.accept()
            }
            Text {
                visible: dialog.op === "between"
                height: 28
                width: 24
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: "y"
                color: Theme.dim
                font.pixelSize: 13
            }
            Field {
                id: second
                visible: dialog.op === "between"
                width: 152
                placeholderText: "Valor"
                onAccepted: dialog.accept()
            }
        }
        Row {
            spacing: 8
            Repeater {
                model: dialog.looks
                Rectangle {
                    required property var modelData
                    required property int index
                    width: 62
                    height: 30
                    radius: 7
                    color: modelData.fill
                    border.color: dialog.look === index ? Theme.tint : "transparent"
                    border.width: 2
                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData.name
                        color: parent.modelData.color
                        font.pixelSize: 12
                        font.weight: bold.on ? Font.Bold : Font.Normal
                    }
                    MouseArea { anchors.fill: parent; onClicked: dialog.look = parent.index }
                }
            }
        }
        Row {
            spacing: 8
            Check { id: bold; onToggled: on = !on }
            Text { height: 18; verticalAlignment: Text.AlignVCenter; text: "En negrita"; color: Theme.text; font.pixelSize: 13 }
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            DialogButton { text: "Cancelar"; onClicked: dialog.close() }
            DialogButton { text: "Añadir"; primary: true; active: !dialog.needsValue || first.text.trim() !== ""; onClicked: dialog.accept() }
        }
    }
}
