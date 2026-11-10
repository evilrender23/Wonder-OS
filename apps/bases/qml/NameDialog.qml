import QtQuick
import QtQuick.Controls.Basic

/// Asks for a name: of a new query, of a table being renamed…
Popup {
    id: dialog
    property string heading
    property string detail
    property string confirm: "Guardar"
    property string problem
    /// Returns what is wrong with the name, or nothing when it was taken.
    property var then: null

    function ask(title, text, name, action, callback) {
        heading = title
        detail = text
        confirm = action
        then = callback
        problem = ""
        field.text = name
        open()
        field.selectAll()
        field.forceActiveFocus()
    }
    function accept() {
        const name = field.text.trim()
        if (name === "")
            return
        const wrong = then ? then(name) : ""
        if (wrong) {
            problem = wrong
            return
        }
        close()
    }

    modal: true
    anchors.centerIn: parent
    width: 360
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#40000000"
    }
    contentItem: Column {
        spacing: 10
        Text {
            width: parent.width
            text: dialog.heading
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: dialog.detail
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Field {
            id: field
            width: parent.width
            wrong: dialog.problem !== ""
            onAccepted: dialog.accept()
            onTextEdited: dialog.problem = ""
        }
        Text {
            width: parent.width
            visible: dialog.problem !== ""
            text: dialog.problem
            color: Theme.tint
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        Item { width: 1; height: 2 }
        Row {
            anchors.right: parent.right
            spacing: 8
            DialogButton {
                text: "Cancelar"
                onClicked: dialog.close()
            }
            DialogButton {
                text: dialog.confirm
                primary: true
                active: field.text.trim() !== ""
                onClicked: dialog.accept()
            }
        }
    }
}
