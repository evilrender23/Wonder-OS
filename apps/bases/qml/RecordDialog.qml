import QtQuick
import QtQuick.Controls.Basic

/// A new record, field by field: for the tables that will not take an empty one.
Popup {
    id: dialog
    property var fields: []
    property string table
    property string problem
    property var values: ({})
    signal accepted(var values)

    function ask(why) {
        values = {}
        problem = why ?? ""
        open()
    }

    parent: Overlay.overlay
    modal: true
    anchors.centerIn: parent
    width: 420
    height: Math.min(parent.height - 60, 150 + Math.min(fields.length, 12) * 34 + (problem !== "" ? 40 : 0))
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
    contentItem: Item {
        Text {
            id: heading
            text: "Registro nuevo en «" + dialog.table + "»"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            width: parent.width
            elide: Text.ElideRight
        }
        Text {
            id: why
            anchors.top: heading.bottom
            anchors.topMargin: 6
            width: parent.width
            visible: dialog.problem !== ""
            height: visible ? implicitHeight : 0
            text: dialog.problem
            color: Theme.tint
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        ListView {
            anchors.top: why.bottom
            anchors.topMargin: 10
            anchors.bottom: buttons.top
            anchors.bottomMargin: 12
            width: parent.width
            clip: true
            spacing: 6
            model: dialog.visible ? dialog.fields : []
            ScrollBar.vertical: ScrollBar {}
            delegate: Row {
                id: line
                required property var modelData
                required property int index
                width: ListView.view.width - 10
                spacing: 10
                Text {
                    width: 130
                    anchors.verticalCenter: parent.verticalCenter
                    text: line.modelData.name + (line.modelData.notNull && !(line.modelData.pk > 0) ? " *" : "")
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Field {
                    width: parent.width - 140
                    placeholderText: line.modelData.pk > 0 && line.modelData.kind === "integer" ? "automático"
                        : (line.modelData.defaultValue ? "por omisión: " + line.modelData.defaultValue : (line.modelData.type ?? ""))
                    onTextEdited: dialog.values[line.modelData.name] = text
                    onAccepted: dialog.accepted(dialog.values)
                }
            }
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 8
            DialogButton {
                text: "Cancelar"
                onClicked: dialog.close()
            }
            DialogButton {
                text: "Añadir"
                primary: true
                onClicked: dialog.accepted(dialog.values)
            }
        }
    }
}
