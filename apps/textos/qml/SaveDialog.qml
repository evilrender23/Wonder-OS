import QtQuick
import QtQuick.Controls.Basic

/// Before a document with changes goes away: keep them, lose them, or stay.
Popup {
    id: dialog
    property string name
    signal save()
    signal discard()

    modal: true
    anchors.centerIn: parent
    width: 400
    padding: 18
    closePolicy: Popup.CloseOnEscape
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
            text: "¿Guardar los cambios de «" + dialog.name + "»?"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: "Si no los guardas, lo escrito desde la última vez se perderá."
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Item { width: 1; height: 2 }
        Item {
            width: parent.width
            height: 28
            DialogButton {
                text: "No guardar"
                danger: true
                onClicked: {
                    dialog.close()
                    dialog.discard()
                }
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
                    onClicked: {
                        dialog.close()
                        dialog.save()
                    }
                }
            }
        }
    }
}
