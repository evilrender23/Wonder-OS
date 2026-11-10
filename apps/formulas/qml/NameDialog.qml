import QtQuick
import QtQuick.Controls.Basic

/// Asks for the name the formula in hand is kept under.
Popup {
    id: dialog
    signal accepted(string name)

    function ask() {
        field.text = ""
        open()
        field.forceActiveFocus()
    }
    function accept() {
        close()
        accepted(field.text.trim())
    }

    modal: true
    anchors.centerIn: parent
    width: 400
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#40000000" }
    contentItem: Column {
        spacing: 12
        Text {
            text: "Guardar en la biblioteca"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Rectangle {
            width: parent.width
            height: 86
            radius: 9
            color: Theme.canvas
            border.color: Theme.line
            Thumb {
                anchors.fill: parent
                anchors.margins: 8
                latex: dialog.visible ? App.latex : ""
                size: 20
            }
        }
        Field {
            id: field
            width: parent.width
            placeholderText: "Nombre de la fórmula"
            onAccepted: dialog.accept()
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            DialogButton {
                text: "Cancelar"
                onClicked: dialog.close()
            }
            DialogButton {
                text: "Guardar"
                primary: true
                onClicked: dialog.accept()
            }
        }
    }
}
