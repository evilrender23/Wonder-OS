import QtQuick
import QtQuick.Controls.Basic

/// Asks for a line of text: a name.
Popup {
    id: dialog
    property string heading
    property string confirm
    property var then: null

    function ask(title, text, action, callback) {
        heading = title
        confirm = action
        then = callback
        field.text = text
        open()
        field.forceActiveFocus()
        field.selectAll()
    }
    function accept() {
        const text = field.text.trim()
        if (text === "")
            return
        const callback = then
        close()
        if (callback)
            callback(text)
    }

    modal: true
    anchors.centerIn: parent
    width: 360
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 12; color: Theme.menu; border.color: Theme.border }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Column {
        spacing: 12
        Text {
            width: parent.width
            text: dialog.heading
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Field {
            id: field
            width: parent.width
            onAccepted: dialog.accept()
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            DialogButton { text: "Cancelar"; onClicked: dialog.close() }
            DialogButton { text: dialog.confirm; primary: true; active: field.text.trim() !== ""; onClicked: dialog.accept() }
        }
    }
}
