import QtQuick
import QtQuick.Controls.Basic

/// Asks before something that is not undone from here.
Popup {
    id: dialog
    property string heading
    property string message
    property string confirm: "Aceptar"
    property var action: null

    function ask(title, text, label, then) {
        heading = title
        message = text
        confirm = label
        action = then
        open()
        okButton.forceActiveFocus()
    }
    function accept() {
        const then = action
        action = null
        close()
        if (then)
            then()
    }

    modal: true
    parent: Overlay.overlay
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
            text: dialog.message
            visible: text !== ""
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Item {
            width: 1
            height: 2
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
                id: okButton
                width: Math.max(86, okText.implicitWidth + 24)
                height: 28
                radius: 7
                color: Theme.tint
                opacity: okArea.pressed ? 0.8 : 1
                Keys.onReturnPressed: dialog.accept()
                Keys.onEnterPressed: dialog.accept()
                Text {
                    id: okText
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
