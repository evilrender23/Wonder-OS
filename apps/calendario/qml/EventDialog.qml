import QtQuick
import QtQuick.Controls.Basic

/// Writes an event, new or old.
Popup {
    id: dialog
    /// Agenda.event()'s fields, changed as the user types.
    property var fields: ({})
    /// The day of the occurrence that was clicked: the one «only this» takes out.
    property int occurrence: 0
    readonly property bool existing: (fields.id ?? "") !== ""
    readonly property bool repeating: existing && wasRepeating
    property bool wasRepeating: false

    readonly property var repeats: [qsTr("Nunca"), qsTr("Cada día"), qsTr("Cada semana"), qsTr("Cada mes"), qsTr("Cada año")]
    readonly property var reminders: [
        { minutes: -1, label: qsTr("Ninguno") },
        { minutes: 0, label: qsTr("A la hora del evento") },
        { minutes: 5, label: qsTr("5 minutos antes") },
        { minutes: 15, label: qsTr("15 minutos antes") },
        { minutes: 30, label: qsTr("30 minutos antes") },
        { minutes: 60, label: qsTr("1 hora antes") },
        { minutes: 120, label: qsTr("2 horas antes") },
        { minutes: 1440, label: qsTr("1 día antes") },
        { minutes: 2880, label: qsTr("2 días antes") },
        { minutes: 10080, label: qsTr("1 semana antes") }
    ]
    /// The list, plus the event's own when it came from elsewhere with another.
    readonly property var reminderOptions: {
        const own = fields.reminder ?? -1
        if (reminders.some(option => option.minutes === own))
            return reminders
        return reminders.concat([{ minutes: own, label: qsTr("%1 minutos antes").arg(own) }]).sort((a, b) => a.minutes - b.minutes)
    }

    function set(key, value) {
        const next = Object.assign({}, fields)
        next[key] = value
        fields = next
    }
    /// Moving the start drags the end along, keeping the length.
    function setStart(day, minute) {
        const next = Object.assign({}, fields)
        const length = (next.endDay - next.startDay) * 1440 + (next.endMin - next.startMin)
        next.startDay = day
        next.startMin = minute
        const end = day * 1440 + minute + Math.max(0, length)
        next.endDay = Math.floor(end / 1440)
        next.endMin = end % 1440
        fields = next
    }
    function setEnd(day, minute) {
        const next = Object.assign({}, fields)
        next.endDay = day
        next.endMin = minute
        // An end before the start pulls the start back.
        if (next.allDay ? day < next.startDay : day * 1440 + minute < next.startDay * 1440 + next.startMin) {
            next.startDay = day
            if (!next.allDay)
                next.startMin = Math.max(0, minute - 60)
        }
        fields = next
    }

    function create(day, minute) {
        fields = App.agenda.blank(day, minute ?? -1)
        occurrence = 0
        wasRepeating = false
        open()
        title.forceActiveFocus()
    }
    function edit(id, day) {
        const found = App.agenda.event(id)
        if (found.id === undefined)
            return
        fields = found
        occurrence = day ?? found.startDay
        wasRepeating = found.repeat !== 0
        open()
        title.forceActiveFocus()
    }
    function accept() {
        App.agenda.save(fields)
        close()
    }

    modal: true
    anchors.centerIn: parent
    width: 460
    padding: 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#40000000"
    }

    component Label: Text {
        width: 92
        height: 28
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignRight
        color: Theme.dim
        font.pixelSize: 13
    }

    contentItem: Column {
        spacing: 10
        Field {
            id: title
            width: parent.width
            implicitHeight: 34
            font.pixelSize: 17
            font.weight: Font.DemiBold
            placeholderText: qsTr("Evento nuevo")
            text: dialog.fields.title ?? ""
            onTextEdited: dialog.set("title", text)
            onAccepted: dialog.accept()
        }
        Row {
            spacing: 10
            Label { text: qsTr("Calendario") }
            FCombo {
                width: 220
                model: App.agenda.calendars.map(calendar => calendar.name)
                colors: App.agenda.calendars.map(calendar => calendar.color)
                currentIndex: App.agenda.calendars.findIndex(calendar => calendar.id === dialog.fields.calendar)
                onActivated: index => dialog.set("calendar", App.agenda.calendars[index].id)
            }
        }
        Row {
            spacing: 10
            Label { text: qsTr("Lugar") }
            Field {
                width: dialog.availableWidth - 102
                placeholderText: qsTr("Añadir lugar")
                text: dialog.fields.location ?? ""
                onTextEdited: dialog.set("location", text)
                onAccepted: dialog.accept()
            }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Row {
            spacing: 10
            Label { text: qsTr("Todo el día") }
            Rectangle {
                id: toggle
                readonly property bool on: dialog.fields.allDay ?? false
                anchors.verticalCenter: parent.verticalCenter
                width: 38
                height: 22
                radius: 11
                color: on ? Theme.accent : Theme.sideSelection
                Rectangle {
                    x: toggle.on ? 18 : 2
                    y: 2
                    width: 18
                    height: 18
                    radius: 9
                    color: "#ffffff"
                    Behavior on x { NumberAnimation { duration: 110 } }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.set("allDay", !toggle.on)
                }
            }
        }
        Row {
            spacing: 10
            Label { text: qsTr("Empieza") }
            DateField {
                day: dialog.fields.startDay ?? 0
                onEdited: day => dialog.setStart(day, dialog.fields.startMin)
            }
            TimeField {
                visible: !(dialog.fields.allDay ?? false)
                minute: dialog.fields.startMin ?? 0
                onEdited: minute => dialog.setStart(dialog.fields.startDay, minute)
            }
        }
        Row {
            spacing: 10
            Label { text: qsTr("Termina") }
            DateField {
                day: dialog.fields.endDay ?? 0
                onEdited: day => dialog.setEnd(day, dialog.fields.endMin)
            }
            TimeField {
                visible: !(dialog.fields.allDay ?? false)
                minute: dialog.fields.endMin ?? 0
                onEdited: minute => dialog.setEnd(dialog.fields.endDay, minute)
            }
        }
        Row {
            spacing: 10
            Label { text: qsTr("Se repite") }
            FCombo {
                width: 132
                model: dialog.repeats
                currentIndex: dialog.fields.repeat ?? 0
                onActivated: index => dialog.set("repeat", index)
            }
            Text {
                visible: (dialog.fields.repeat ?? 0) !== 0
                height: 28
                verticalAlignment: Text.AlignVCenter
                text: qsTr("hasta")
                color: Theme.dim
                font.pixelSize: 13
            }
            DateField {
                visible: (dialog.fields.repeat ?? 0) !== 0
                optional: true
                width: 126
                placeholder: qsTr("siempre")
                day: dialog.fields.untilDay ?? 0
                onEdited: day => dialog.set("untilDay", day)
            }
        }
        Row {
            spacing: 10
            Label { text: qsTr("Aviso") }
            FCombo {
                width: 220
                model: dialog.reminderOptions.map(option => option.label)
                currentIndex: dialog.reminderOptions.findIndex(option => option.minutes === (dialog.fields.reminder ?? -1))
                onActivated: index => dialog.set("reminder", dialog.reminderOptions[index].minutes)
            }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        ScrollView {
            width: parent.width
            height: 84
            TextArea {
                id: notes
                placeholderText: qsTr("Notas")
                placeholderTextColor: Theme.faint
                color: Theme.text
                font.pixelSize: 13
                wrapMode: TextEdit.Wrap
                selectionColor: Theme.accent
                selectedTextColor: "#ffffff"
                selectByMouse: true
                leftPadding: 9
                rightPadding: 9
                text: dialog.fields.notes ?? ""
                onTextChanged: if (activeFocus) dialog.set("notes", text)
                background: Rectangle {
                    radius: 7
                    color: Theme.field
                    border.color: notes.activeFocus ? Theme.accent : "transparent"
                }
            }
        }
        Item {
            width: parent.width
            height: 28
            DialogButton {
                id: removeButton
                visible: dialog.existing
                text: qsTr("Eliminar")
                danger: true
                onClicked: {
                    if (dialog.repeating) {
                        removeMenu.popup(removeButton, 0, removeButton.height + 4)
                    } else {
                        App.agenda.remove(dialog.fields.id)
                        dialog.close()
                    }
                }
                FMenu {
                    id: removeMenu
                    FMenuItem {
                        text: qsTr("Solo este evento")
                        onTriggered: {
                            App.agenda.skip(dialog.fields.id, dialog.occurrence)
                            dialog.close()
                        }
                    }
                    FMenuItem {
                        text: qsTr("Todos los eventos de la serie")
                        onTriggered: {
                            App.agenda.remove(dialog.fields.id)
                            dialog.close()
                        }
                    }
                }
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                DialogButton {
                    text: qsTr("Cancelar")
                    onClicked: dialog.close()
                }
                DialogButton {
                    text: dialog.existing ? qsTr("Guardar") : qsTr("Añadir")
                    primary: true
                    onClicked: dialog.accept()
                }
            }
        }
    }
}
