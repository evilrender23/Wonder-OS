import QtQuick
import QtQuick.Controls.Basic

/// The name and the colour of a calendar, new or old.
Popup {
    id: dialog
    /// Empty for a new one.
    property string calendarId
    property string tone: App.agenda.palette[4]

    function create() {
        calendarId = ""
        // The first colour no calendar has yet.
        const used = App.agenda.calendars.map(calendar => calendar.color)
        tone = App.agenda.palette.find(color => !used.includes(color)) ?? App.agenda.palette[4]
        field.text = ""
        open()
        field.forceActiveFocus()
    }
    function edit(id) {
        const found = App.agenda.calendar(id)
        if (found.id === undefined)
            return
        calendarId = id
        tone = found.color
        field.text = found.name
        open()
        field.selectAll()
        field.forceActiveFocus()
    }
    function accept() {
        if (field.text.trim() === "")
            return
        if (calendarId === "")
            App.agenda.addCalendar(field.text, tone)
        else
            App.agenda.editCalendar(calendarId, field.text, tone)
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
        spacing: 14
        Text {
            text: dialog.calendarId === "" ? "Calendario nuevo" : "Editar calendario"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Field {
            id: field
            width: parent.width
            implicitHeight: 30
            placeholderText: "Nombre del calendario"
            onAccepted: dialog.accept()
        }
        Row {
            spacing: 10
            Repeater {
                model: App.agenda.palette
                Rectangle {
                    required property string modelData
                    readonly property bool on: Qt.colorEqual(modelData, dialog.tone)
                    width: 26
                    height: 26
                    radius: 13
                    color: "transparent"
                    border.width: 2
                    border.color: on ? modelData : "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        radius: 9
                        color: parent.modelData
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: dialog.tone = parent.modelData
                    }
                }
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
                text: dialog.calendarId === "" ? "Crear" : "Guardar"
                primary: true
                active: field.text.trim() !== ""
                onClicked: dialog.accept()
            }
        }
    }
}
