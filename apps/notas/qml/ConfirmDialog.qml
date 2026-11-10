import QtQuick
import QtQuick.Controls.Basic

/// Asks before something that cannot be undone.
Popup {
    id: dialog
    property string heading
    property string detail
    property string confirm
    property var then: null

    function ask(title, text, action, callback) {
        heading = title
        detail = text
        confirm = action
        then = callback
        open()
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
            text: dialog.detail
            color: Theme.dim
            font.pixelSize: 13
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
                danger: true
                onClicked: {
                    const callback = dialog.then
                    dialog.close()
                    if (callback)
                        callback()
                }
            }
        }
    }
}
