import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win

    /// day, week, month or year.
    property string mode: App.setting("mode", "month")
    /// The day in focus, a Julian day number: what the view is built around.
    property int day: App.today
    readonly property bool searching: search.text.trim() !== ""
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool busy: eventDialog.visible || calendarDialog.visible || confirmDialog.visible

    width: App.testing ? App.testSize.width : App.setting("width", 1120)
    height: App.testing ? App.testSize.height : App.setting("height", 720)
    minimumWidth: 880
    minimumHeight: 480
    visible: true
    title: qsTr("Calendario")
    color: App.glass ? "transparent" : Theme.window

    onClosing: {
        if (App.testing)
            return
        App.setSetting("width", width)
        App.setSetting("height", height)
        App.setSetting("mode", mode)
    }
    Component.onCompleted: {
        if (!["day", "week", "month", "year"].includes(mode))
            mode = "month"
        if (App.testing)
            testStep.start()
    }

    // --- Where the window is ---------------------------------------------------

    readonly property string heading: {
        const focus = App.parts(day)
        const month = name => name.charAt(0).toUpperCase() + name.slice(1)
        switch (mode) {
        case "year":
            return String(focus.year)
        case "day":
            // In a narrow window the long date would not fit beside the controls.
            //: A short date: %1 the weekday, %2 the day, %3 the month, %4 the year.
            return bar.width < 780 ? qsTr("%1 %2 %3 %4").arg(month(App.weekdayName(focus.weekday)).slice(0, 3)).arg(focus.day).arg(App.monthName(focus.month).slice(0, 3)).arg(focus.year)
                : month(App.longDate(day))
        case "week": {
            // A week may straddle two months, or two years.
            const first = App.parts(day - focus.weekday)
            const last = App.parts(day - focus.weekday + 6)
            if (first.month === last.month)
                return month(App.monthName(first.month)) + " " + first.year
            if (first.year === last.year)
                return month(App.monthName(first.month)).slice(0, 3) + " – " + App.monthName(last.month).slice(0, 3) + " " + last.year
            return month(App.monthName(first.month)).slice(0, 3) + " " + first.year + " – " + App.monthName(last.month).slice(0, 3) + " " + last.year
        }
        default:
            return month(App.monthName(focus.month)) + " " + focus.year
        }
    }

    function step(direction) {
        switch (mode) {
        case "day":
            day += direction
            break
        case "week":
            day += 7 * direction
            break
        case "year":
            day = App.addMonths(day, 12 * direction)
            break
        default:
            day = App.addMonths(day, direction)
        }
    }
    function goToday() {
        day = App.today
    }
    function show(what) {
        search.text = ""
        mode = what
    }
    function newEvent(onDay, minute) {
        eventDialog.create(onDay ?? day, minute ?? -1)
    }
    function editEvent(event) {
        eventDialog.edit(event.id, event.from ?? event.startDay)
    }

    Connections {
        target: App
        function onRaiseRequested() {
            win.show()
            win.raise()
            win.requestActivate()
        }
        function onSaid(message) {
            toast.say(message)
        }
    }

    // --- Keys ----------------------------------------------------------------------

    Shortcut {
        sequence: "Ctrl+N"
        enabled: !win.busy
        onActivated: win.newEvent()
    }
    Shortcut {
        sequence: "Ctrl+T"
        enabled: !win.busy
        onActivated: win.goToday()
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: !win.busy
        onActivated: win.show("day")
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: !win.busy
        onActivated: win.show("week")
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: !win.busy
        onActivated: win.show("month")
    }
    Shortcut {
        sequence: "Ctrl+4"
        enabled: !win.busy
        onActivated: win.show("year")
    }
    Shortcut {
        sequence: "Left"
        enabled: !win.busy && !win.typing && !win.searching
        onActivated: win.step(-1)
    }
    Shortcut {
        sequence: "Right"
        enabled: !win.busy && !win.typing && !win.searching
        onActivated: win.step(1)
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: !win.busy
        onActivated: {
            search.forceActiveFocus()
            search.selectAll()
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: search.activeFocus && !win.busy
        onActivated: {
            search.text = ""
            content.forceActiveFocus()
        }
    }
    Shortcut {
        sequences: [StandardKey.Quit, StandardKey.Close]
        onActivated: win.close()
    }

    // --- Menus and dialogs ------------------------------------------------------------

    EventDialog {
        id: eventDialog
    }
    CalendarDialog {
        id: calendarDialog
    }
    ConfirmDialog {
        id: confirmDialog
    }
    FileDialog {
        id: importDialog
        property string into
        title: qsTr("Importar calendario")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Calendarios (*.ics *.ical *.ifb *.icalendar)"), qsTr("Todos los archivos (*)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            let total = 0
            let failed = 0
            for (const file of selectedFiles) {
                const read = App.agenda.importFile(App.localPath(file), into)
                if (read < 0)
                    ++failed
                else
                    total += read
            }
            const name = App.agenda.calendar(into === "" ? App.agenda.defaultCalendar : into).name
            if (failed > 0 && total === 0)
                toast.say(failed === 1 ? qsTr("El archivo no es un calendario que se pueda leer.") : qsTr("Los archivos no son calendarios que se puedan leer."))
            else
                toast.say(total === 1 ? qsTr("1 evento añadido a «%1».").arg(name) : qsTr("%1 eventos añadidos a «%2».").arg(total).arg(name))
        }
    }
    FileDialog {
        id: exportDialog
        property string from
        title: qsTr("Exportar calendario")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ics"
        nameFilters: [qsTr("Calendarios (*.ics)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: toast.say(App.agenda.exportFile(App.localPath(selectedFile), from) ? qsTr("Calendario exportado.") : qsTr("No se ha podido escribir el archivo."))
    }

    FMenu {
        id: calendarMenu
        property string calendarId
        readonly property var info: App.agenda.revision >= 0 ? App.agenda.calendar(calendarId) : ({})
        function showFor(id) {
            calendarId = id
            popup()
        }
        FMenuItem {
            text: qsTr("Editar…")
            onTriggered: calendarDialog.edit(calendarMenu.calendarId)
        }
        FMenuItem {
            text: qsTr("Usar para los eventos nuevos")
            checked: App.agenda.defaultCalendar === calendarMenu.calendarId
            onTriggered: App.agenda.defaultCalendar = calendarMenu.calendarId
        }
        FMenuItem {
            text: qsTr("Mostrar solo este")
            onTriggered: {
                for (const calendar of App.agenda.calendars)
                    App.agenda.setVisible(calendar.id, calendar.id === calendarMenu.calendarId)
            }
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Importar aquí…")
            onTriggered: {
                importDialog.into = calendarMenu.calendarId
                importDialog.open()
            }
        }
        FMenuItem {
            text: qsTr("Exportar…")
            onTriggered: {
                exportDialog.from = calendarMenu.calendarId
                exportDialog.open()
            }
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Eliminar…")
            enabled: App.agenda.calendars.length > 1
            onTriggered: {
                const info = calendarMenu.info
                const id = calendarMenu.calendarId
                if (info.events === 0) {
                    App.agenda.removeCalendar(id)
                    return
                }
                confirmDialog.ask(qsTr("¿Eliminar el calendario «%1»?").arg(info.name),
                    info.events === 1 ? qsTr("Su evento se eliminará con él. Esto no se puede deshacer.")
                        : qsTr("Sus %1 eventos se eliminarán con él. Esto no se puede deshacer.").arg(info.events),
                    qsTr("Eliminar"), () => App.agenda.removeCalendar(id))
            }
        }
    }
    FMenu {
        id: moreMenu
        FMenuItem {
            text: qsTr("Calendario nuevo…")
            onTriggered: calendarDialog.create()
        }
        FSeparator {}
        FMenuItem {
            text: qsTr("Importar…")
            onTriggered: {
                importDialog.into = ""
                importDialog.open()
            }
        }
        FMenuItem {
            text: qsTr("Exportar los calendarios visibles…")
            onTriggered: {
                exportDialog.from = ""
                exportDialog.open()
            }
        }
    }

    // --- The window ------------------------------------------------------------------

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 224
            color: Theme.sidebar

            Rectangle {
                id: newButton
                x: 12
                y: 12
                width: parent.width - 24
                height: 30
                radius: 7
                color: newArea.containsMouse ? Theme.sideSelection : Theme.field
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Sym {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "plus"
                        size: 15
                        color: Theme.tint
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Evento nuevo")
                        color: Theme.text
                        font.pixelSize: 13
                    }
                }
                MouseArea {
                    id: newArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: win.newEvent()
                }
            }

            MiniMonth {
                id: mini
                x: 14
                y: 56
                selected: win.day
                marks: true
                // Follows the day in focus, and wanders on its own with the arrows.
                property int shown: App.monthStart(App.parts(win.day).year, App.parts(win.day).month)
                Connections {
                    target: win
                    function onDayChanged() {
                        mini.shown = App.monthStart(App.parts(win.day).year, App.parts(win.day).month)
                    }
                }
                year: App.parts(shown).year
                month: App.parts(shown).month
                onShifted: months => shown = App.addMonths(shown, months)
                onPicked: picked => {
                    search.text = ""
                    win.day = picked
                }
            }

            Text {
                id: calendarsLabel
                x: 20
                y: mini.y + mini.height + 14
                text: qsTr("Calendarios")
                color: Theme.dim
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            ListView {
                x: 10
                y: calendarsLabel.y + 22
                width: parent.width - 20
                height: parent.height - y - 46
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: App.agenda.calendars
                delegate: Rectangle {
                    id: entry
                    required property var modelData
                    width: ListView.view.width
                    height: 28
                    radius: 6
                    color: entryArea.containsMouse ? Theme.hover : "transparent"
                    Rectangle {
                        id: box
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        radius: 4
                        color: entry.modelData.visible ? entry.modelData.color : "transparent"
                        border.width: 1.5
                        border.color: entry.modelData.color
                        Sym {
                            anchors.centerIn: parent
                            visible: entry.modelData.visible
                            name: "check"
                            size: 11
                            color: "#ffffff"
                        }
                    }
                    Text {
                        anchors.left: box.right
                        anchors.leftMargin: 9
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.name
                        color: Theme.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: entryArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton)
                                calendarMenu.showFor(entry.modelData.id)
                            else
                                App.agenda.setVisible(entry.modelData.id, !entry.modelData.visible)
                        }
                        onDoubleClicked: mouse => {
                            if (mouse.button === Qt.LeftButton) {
                                // The second click hid it again: put it back and edit.
                                App.agenda.setVisible(entry.modelData.id, true)
                                calendarDialog.edit(entry.modelData.id)
                            }
                        }
                    }
                }
            }
            TButton {
                x: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                symbol: "ellipsis"
                tone: Theme.dim
                onClicked: moreMenu.popup()
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.window

            Item {
                id: bar
                width: parent.width
                height: 52
                Text {
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, controls.x - 32)
                    text: win.searching ? qsTr("Buscar") : win.heading
                    color: Theme.text
                    font.pixelSize: 21
                    font.weight: Font.DemiBold
                    // A long day («miércoles, 30 de septiembre de 2026») shrinks before it is cut.
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 14
                    elide: Text.ElideRight
                }
                Row {
                    id: controls
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !win.searching
                        spacing: 2
                        TButton {
                            symbol: "chevron-left"
                            onClicked: win.step(-1)
                        }
                        Rectangle {
                            width: todayLabel.implicitWidth + 20
                            height: 26
                            radius: 6
                            color: todayArea.containsMouse ? Theme.hover : "transparent"
                            Text {
                                id: todayLabel
                                anchors.centerIn: parent
                                text: qsTr("Hoy")
                                color: Theme.text
                                font.pixelSize: 13
                            }
                            MouseArea {
                                id: todayArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: win.goToday()
                            }
                        }
                        TButton {
                            symbol: "chevron-right"
                            onClicked: win.step(1)
                        }
                    }
                    Segmented {
                        anchors.verticalCenter: parent.verticalCenter
                        options: [
                            { key: "day", label: qsTr("Día") },
                            { key: "week", label: qsTr("Semana") },
                            { key: "month", label: qsTr("Mes") },
                            { key: "year", label: qsTr("Año") }
                        ]
                        current: win.searching ? "" : win.mode
                        onPicked: key => win.show(key)
                    }
                    Field {
                        id: search
                        anchors.verticalCenter: parent.verticalCenter
                        width: bar.width < 780 ? 120 : 190
                        leftPadding: 28
                        rightPadding: 26
                        placeholderText: qsTr("Buscar")
                        Sym {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            name: "search"
                            size: 14
                            color: Theme.dim
                        }
                        TButton {
                            visible: search.text !== ""
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            size: 12
                            symbol: "x"
                            tone: Theme.dim
                            onClicked: search.text = ""
                        }
                    }
                }
            }
            Rectangle {
                y: bar.height - 1
                width: parent.width
                height: 1
                color: win.mode === "year" || win.searching ? Theme.border : "transparent"
            }

            FocusScope {
                id: content
                y: bar.height
                width: parent.width
                height: parent.height - y
                Loader {
                    anchors.fill: parent
                    sourceComponent: win.searching ? searchPage
                        : win.mode === "year" ? yearPage
                        : win.mode === "month" ? monthPage
                        : win.mode === "week" ? weekPage : dayPage
                }
            }

            // A word about what just happened.
            Rectangle {
                id: toast
                function say(message) {
                    toastText.text = message
                    opacity = 1
                    toastTimer.restart()
                }
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 18
                width: Math.min(parent.width - 40, toastText.implicitWidth + 32)
                height: 34
                radius: 17
                color: Theme.dark ? "#f0404044" : "#f02c2c2e"
                opacity: 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 160 } }
                Text {
                    id: toastText
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 24)
                    color: "#ffffff"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Timer {
                    id: toastTimer
                    interval: 3200
                    onTriggered: toast.opacity = 0
                }
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => App.open(drop.urls)
    }

    Component {
        id: monthPage
        MonthView {
            day: win.day
            onPicked: day => win.day = day
            onCreate: day => win.newEvent(day)
            onEdit: event => win.editEvent(event)
            onOpened: day => {
                win.day = day
                win.mode = "day"
            }
            onShifted: months => win.step(months)
        }
    }
    Component {
        id: weekPage
        WeekView {
            day: win.day
            days: 7
            onPicked: day => win.day = day
            onCreate: (day, minute) => win.newEvent(day, minute)
            onEdit: event => win.editEvent(event)
        }
    }
    Component {
        id: dayPage
        WeekView {
            day: win.day
            days: 1
            onPicked: day => win.day = day
            onCreate: (day, minute) => win.newEvent(day, minute)
            onEdit: event => win.editEvent(event)
        }
    }
    Component {
        id: yearPage
        YearView {
            day: win.day
            onPicked: day => {
                win.day = day
                win.mode = "day"
            }
            onMonthPicked: day => {
                win.day = day
                win.mode = "month"
            }
            onShifted: years => win.step(years)
        }
    }
    Component {
        id: searchPage
        SearchView {
            text: search.text
            onEdit: event => win.editEvent(event)
        }
    }

    // --- Pictures for the tests (--shot, --do) --------------------------------------

    /// A fortnight of made-up life around today, to have something to look at.
    function sample() {
        const agenda = App.agenda
        const monday = App.today - App.parts(App.today).weekday
        const sport = agenda.addCalendar("Deporte", "#34c759")
        const add = (title, calendar, offset, from, to, more) => agenda.save(Object.assign(agenda.blank(monday + offset, from),
            { title: title, calendar: calendar, endDay: monday + offset, endMin: to }, more ?? {}))
        add("Reunión de equipo", "trabajo", 0, 570, 630, { repeat: 2, location: "Sala 2" })
        add("Comida con Ana", "personal", 1, 840, 930, { location: "La Tasca", reminder: 30 })
        add("Dentista", "personal", 1, 1020, 1065)
        add("Revisión del proyecto", "trabajo", 1, 600, 720)
        add("Llamada con el cliente", "trabajo", 1, 660, 705)
        add("Correr", sport, 0, 420, 465, { repeat: 1 })
        add("Presentación", "trabajo", 3, 720, 810, { reminder: 15 })
        add("Cine", "personal", 4, 1230, 1380)
        add("Pádel", sport, 5, 660, 750)
        add("Entrega", "trabajo", 9, 540, 600)
        add("Cena de cumpleaños", "personal", 12, 1260, 1410)
        add("Puente", "personal", 10, 0, 0, { allDay: true, endDay: monday + 11 })
        add("Congreso", "trabajo", 2, 0, 0, { allDay: true, endDay: monday + 3 })
        add("Cumpleaños de Luis", "personal", 6, 0, 0, { allDay: true, repeat: 4 })
    }
    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        switch (verb) {
        case "sample":
            sample()
            break
        case "view":
            show(arg)
            break
        case "day":
            day = App.parseDate(arg) || day
            break
        case "step":
            step(Number(arg))
            break
        case "search":
            search.text = arg
            break
        case "new":
            newEvent()
            break
        case "edit": {
            const events = App.agenda.day(day)
            if (events.length > Number(arg))
                editEvent(events[Number(arg)])
            break
        }
        case "calendar":
            calendarDialog.create()
            break
        case "hide":
            App.agenda.setVisible(arg, false)
            break
        case "menu":
            calendarMenu.showFor(App.agenda.calendars[Number(arg)].id)
            break
        case "say":
            toast.say(arg)
            break
        }
    }
    Timer {
        id: testStep
        interval: 300
        onTriggered: {
            for (const action of App.testActions)
                win.runAction(action.trim())
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 900
        onTriggered: App.saveShot(win)
    }
}
