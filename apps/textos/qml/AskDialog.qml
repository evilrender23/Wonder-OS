import QtQuick
import QtQuick.Controls.Basic

/// Asks for a value or two before doing something with them.
Popup {
    id: dialog
    property string heading
    property string confirm: "Aceptar"
    /// [{label, value, placeholder}]
    property var fields: []
    property var then: null

    function ask(title, what, action, callback) {
        heading = title
        fields = what
        confirm = action
        then = callback
        open()
    }
    function accept() {
        const values = []
        for (let i = 0; i < inputs.count; ++i)
            values.push(inputs.itemAt(i).value)
        const callback = then
        close()
        if (callback)
            callback(values)
    }

    modal: true
    anchors.centerIn: parent
    width: 360
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: if (inputs.count > 0) inputs.itemAt(0).grab()
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
        Repeater {
            id: inputs
            model: dialog.fields
            Column {
                required property var modelData
                readonly property string value: input.text
                function grab() {
                    input.forceActiveFocus()
                    input.selectAll()
                }
                width: parent.width
                spacing: 4
                Text {
                    text: parent.modelData.label
                    color: Theme.dim
                    font.pixelSize: 12
                }
                Field {
                    id: input
                    width: parent.width
                    text: parent.modelData.value ?? ""
                    placeholderText: parent.modelData.placeholder ?? ""
                    onAccepted: dialog.accept()
                }
            }
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
                onClicked: dialog.accept()
            }
        }
    }
}
