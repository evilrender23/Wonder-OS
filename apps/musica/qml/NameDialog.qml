import QtQuick
import QtQuick.Controls.Basic

/// Asks for the name of a playlist.
Popup {
    id: dialog
    property string heading
    property string confirm: "Crear"
    signal accepted(string name)

    function ask(title, name, action) {
        heading = title
        confirm = action
        field.text = name
        open()
        field.selectAll()
        field.forceActiveFocus()
    }
    function accept() {
        if (field.text.trim() === "")
            return
        accepted(field.text.trim())
        close()
    }

    modal: true
    anchors.centerIn: parent
    width: 340
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
        spacing: 12
        Text {
            text: dialog.heading
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        TextField {
            id: field
            width: parent.width
            placeholderText: "Nombre de la playlist"
            placeholderTextColor: Theme.faint
            color: Theme.text
            font.pixelSize: 13
            selectionColor: Theme.tint
            selectedTextColor: "#ffffff"
            leftPadding: 9
            rightPadding: 9
            background: Rectangle {
                implicitHeight: 30
                radius: 7
                color: Theme.field
                border.color: field.activeFocus ? Theme.tint : "transparent"
            }
            onAccepted: dialog.accept()
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            Rectangle {
                width: 86
                height: 28
                radius: 7
                color: cancelArea.containsMouse ? Theme.sideSelection : Theme.field
                Text {
                    anchors.centerIn: parent
                    text: "Cancelar"
                    color: Theme.text
                    font.pixelSize: 13
                }
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: dialog.close()
                }
            }
            Rectangle {
                width: 86
                height: 28
                radius: 7
                color: Theme.tint
                opacity: field.text.trim() === "" ? 0.45 : (okArea.pressed ? 0.8 : 1)
                Text {
                    anchors.centerIn: parent
                    text: dialog.confirm
                    color: "#ffffff"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: okArea
                    anchors.fill: parent
                    onClicked: dialog.accept()
                }
            }
        }
    }
}
