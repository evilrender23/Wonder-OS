import QtQuick
import QtQuick.Controls.Basic

/// Asks for the values of the parameters of a query (`:desde`, `:ciudad`…).
Popup {
    id: dialog
    property var names: []
    property var values: ({})
    property var then: null

    function ask(parameters, known, callback) {
        names = parameters
        const next = {}
        for (const name of parameters)
            next[name] = known[name] ?? ""
        values = next
        then = callback
        open()
    }
    function accept() {
        const callback = then
        close()
        if (callback)
            callback(values)
    }

    modal: true
    anchors.centerIn: parent
    width: 380
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
            text: "Valores de la consulta"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            text: "La consulta pide estos valores. Se enlazan, no se escriben dentro del SQL."
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Repeater {
            model: dialog.names
            Row {
                id: line
                required property string modelData
                required property int index
                width: parent.width
                spacing: 10
                Text {
                    width: 110
                    anchors.verticalCenter: parent.verticalCenter
                    text: line.modelData
                    color: Theme.text
                    font.pixelSize: 13
                    font.family: Theme.mono
                    elide: Text.ElideRight
                }
                Field {
                    width: parent.width - 120
                    text: dialog.values[line.modelData] ?? ""
                    focus: line.index === 0
                    onTextEdited: dialog.values[line.modelData] = text
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
                text: "Ejecutar"
                primary: true
                onClicked: dialog.accept()
            }
        }
    }
}
