import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win

    /// The device whose details are open, by address.
    property string shown: ""
    readonly property var shownDevice: (Bt.revision, shown === "" ? ({}) : Bt.device(shown))
    readonly property var request: Bt.request
    readonly property bool asking: request.kind !== undefined

    width: App.testing ? App.testSize.width : 620
    height: App.testing ? App.testSize.height : 720
    minimumWidth: 440
    minimumHeight: 540
    visible: true
    title: "Bluetooth"
    color: Theme.window

    palette.window: Theme.card
    palette.windowText: Theme.text
    palette.base: Theme.card
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#ffffff"
    palette.toolTipBase: Theme.card
    palette.toolTipText: Theme.text

    // Looking for devices costs battery to everything around: only while someone is looking.
    Binding {
        target: Bt
        property: "watching"
        value: win.visible && win.visibility !== Window.Minimized
    }

    onShownDeviceChanged: {
        if (shown !== "" && shownDevice.address === undefined) {
            details.close()
        }
    }
    onAskingChanged: {
        if (asking) {
            answerField.text = ""
            pairing.open()
        } else {
            pairing.close()
        }
    }
    Component.onCompleted: {
        if (App.testing)
            testStep.start()
    }

    Connections {
        target: App
        function onRaiseRequested() {
            win.show()
            win.raise()
            win.requestActivate()
        }
    }
    Connections {
        target: Bt
        function onNotice(text) {
            notice.text = text
            noticeTimer.restart()
        }
    }

    Shortcut {
        sequence: StandardKey.Quit
        onActivated: Qt.quit()
    }
    Shortcut {
        sequence: StandardKey.Close
        onActivated: win.close()
    }

    function showDetails(address) {
        shown = address
        forgetButton.armed = false
        details.open()
    }

    component SectionTitle: Text {
        color: Theme.dim
        font.pixelSize: Theme.small
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.4
        leftPadding: 14
        topPadding: 10
    }
    component Card: Rectangle {
        default property alias content: inner.data
        width: parent ? parent.width : 0
        height: inner.height
        radius: Theme.radius
        color: Theme.card
        border.width: 1
        border.color: Theme.border
        clip: true
        Column {
            id: inner
            width: parent.width
        }
    }
    component Line: Rectangle {
        width: parent ? parent.width - 14 : 0
        x: 14
        height: 1
        color: Theme.border
    }
    component SettingRow: Item {
        property alias label: rowLabel.text
        property string note: ""
        default property alias control: slot.data
        width: parent ? parent.width : 0
        height: Math.max(46, labels.implicitHeight + 16)
        RowLayout {
            anchors { fill: parent; leftMargin: 14; rightMargin: 12 }
            spacing: 12
            ColumnLayout {
                id: labels
                Layout.fillWidth: true
                spacing: 1
                Text {
                    id: rowLabel
                    Layout.fillWidth: true
                    color: Theme.text
                    font.pixelSize: Theme.body
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: note
                    color: Theme.dim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                id: slot
                spacing: 8
            }
        }
    }
    component Placeholder: Column {
        property alias symbol: glyph.name
        property alias title: heading.text
        property alias body: para.text
        default property alias extra: more.data
        width: parent ? parent.width : 0
        topPadding: 56
        spacing: 8
        Sym {
            id: glyph
            anchors.horizontalCenter: parent.horizontalCenter
            size: 44
            color: Theme.faint
        }
        Text {
            id: heading
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.DemiBold
            topPadding: 8
        }
        Text {
            id: para
            width: Math.min(parent.width, 380)
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            color: Theme.dim
            font.pixelSize: Theme.body
            wrapMode: Text.WordWrap
        }
        Item {
            id: more
            width: childrenRect.width
            height: childrenRect.height + 10
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: page.height + 48 + (toast.visible ? toast.height + 20 : 0)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: page
            width: Math.min(flick.width - 40, 640)
            x: Math.round((flick.width - width) / 2)
            y: 24
            spacing: 8

            Card {
                Item {
                    width: parent.width
                    height: 68
                    RowLayout {
                        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                        spacing: 12
                        Rectangle {
                            implicitWidth: 40
                            implicitHeight: 40
                            radius: 10
                            color: Bt.powered ? Theme.accent : Theme.plate
                            Sym {
                                anchors.centerIn: parent
                                name: Bt.powered ? "bluetooth" : "bluetooth-off"
                                size: 22
                                color: Bt.powered ? "#ffffff" : Theme.dim
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: "Bluetooth"
                                color: Theme.text
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: !Bt.present ? qsTr("No hay adaptador")
                                    : !Bt.powered ? qsTr("Desactivado")
                                    : Bt.discoverable ? qsTr("Visible como «%1»").arg(Bt.name)
                                    : qsTr("Activado")
                                color: Theme.dim
                                font.pixelSize: Theme.small
                                elide: Text.ElideRight
                            }
                        }
                        TSwitch {
                            enabled: Bt.present
                            on: Bt.powered
                            Accessible.name: "Bluetooth"
                            onAsked: value => Bt.setPowered(value)
                        }
                    }
                }
            }

            Placeholder {
                visible: !Bt.present
                symbol: "bluetooth-off"
                title: qsTr("No hay adaptador Bluetooth")
                body: qsTr("Este equipo no tiene Bluetooth, o el servicio no está en marcha. Si usas un adaptador USB, conéctalo y aparecerá aquí.")
            }
            Placeholder {
                visible: Bt.present && !Bt.powered
                symbol: "bluetooth-off"
                title: qsTr("Bluetooth está desactivado")
                body: qsTr("Actívalo para conectar auriculares, ratones, teclados, mandos y otros dispositivos.")
                TButton {
                    primary: true
                    text: qsTr("Activar Bluetooth")
                    onClicked: Bt.setPowered(true)
                }
            }

            SectionTitle {
                visible: mineCard.visible
                text: qsTr("Mis dispositivos")
            }
            Card {
                id: mineCard
                visible: Bt.present && Bt.mine.count > 0
                Repeater {
                    model: Bt.mine
                    DeviceRow {
                        known: true
                        onDetails: win.showDetails(device.address)
                    }
                }
            }

            Row {
                visible: Bt.powered
                spacing: 8
                SectionTitle {
                    text: qsTr("Dispositivos cercanos")
                }
                Spinner {
                    visible: Bt.scanning
                    size: 13
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 1
                }
            }
            Card {
                visible: Bt.powered
                Repeater {
                    model: Bt.nearby
                    DeviceRow {}
                }
                Text {
                    visible: Bt.nearby.count === 0
                    width: parent.width
                    padding: 18
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    font.pixelSize: Theme.body
                    text: Bt.scanning ? qsTr("Buscando dispositivos… Pon el tuyo en modo de enlace para que aparezca.")
                                      : qsTr("No se ha encontrado ningún dispositivo.")
                }
            }

            SectionTitle {
                visible: Bt.powered
                text: qsTr("Este equipo")
            }
            Card {
                visible: Bt.powered
                SettingRow {
                    label: qsTr("Nombre")
                    note: qsTr("Así ven los demás dispositivos a este equipo.")
                    TField {
                        id: nameField
                        implicitWidth: 210
                        text: Bt.name
                        maximumLength: 64
                        Accessible.name: qsTr("Nombre de este equipo")
                        onEditingFinished: {
                            Bt.rename(text)
                            text = Qt.binding(() => Bt.name)
                        }
                        Keys.onEscapePressed: {
                            text = Qt.binding(() => Bt.name)
                            focus = false
                        }
                    }
                }
                Line {}
                SettingRow {
                    label: qsTr("Visible para otros dispositivos")
                    note: Bt.discoverable && Bt.discoverableMinutes > 0
                        ? qsTr("Deja de estarlo sola a los %1 min.").arg(Bt.discoverableMinutes)
                        : qsTr("Solo hace falta para enlazar desde el otro dispositivo.")
                    TSwitch {
                        on: Bt.discoverable
                        Accessible.name: qsTr("Visible para otros dispositivos")
                        onAsked: value => Bt.setDiscoverable(value)
                    }
                }
                Line {}
                SettingRow {
                    label: qsTr("Dirección")
                    Text {
                        text: Bt.address
                        color: Theme.dim
                        font.pixelSize: Theme.body
                        font.family: "monospace"
                    }
                }
            }
        }
    }

    // --- What went wrong ---------------------------------------------------

    Rectangle {
        id: toast
        visible: opacity > 0
        opacity: noticeTimer.running ? 1 : 0
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 20 }
        width: Math.min(parent.width - 40, 520)
        height: notice.implicitHeight + 24
        radius: 10
        z: 5
        color: Theme.dark ? "#3a3a3c" : "#1d1d1f"
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text {
            id: notice
            anchors { fill: parent; margins: 12; rightMargin: 36 }
            color: "#ffffff"
            font.pixelSize: Theme.body
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Sym {
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            name: "x"
            size: 14
            color: "#ffffff"
            TapHandler { onTapped: noticeTimer.stop() }
        }
        Timer {
            id: noticeTimer
            interval: 7000
        }
    }

    // --- One device --------------------------------------------------------

    component Sheet: Popup {
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent.width - 40, 420)
        modal: true
        focus: true
        padding: 20
        background: Rectangle {
            radius: 14
            color: Theme.card
            border.width: 1
            border.color: Theme.border
        }
        Overlay.modal: Rectangle { color: Theme.scrim }
    }

    Sheet {
        id: details
        onClosed: win.shown = ""

        contentItem: Column {
            spacing: 14

            RowLayout {
                width: parent.width
                spacing: 12
                Rectangle {
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: 22
                    color: win.shownDevice.connected ? Theme.accent : Theme.plate
                    Sym {
                        anchors.centerIn: parent
                        name: win.shownDevice.symbol ?? ""
                        size: 22
                        color: win.shownDevice.connected ? "#ffffff" : Theme.text
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: win.shownDevice.kindLabel ?? ""
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: win.shownDevice.status ?? ""
                        color: Theme.dim
                        font.pixelSize: Theme.small
                        elide: Text.ElideRight
                    }
                }
                TButton {
                    implicitWidth: 30
                    leftPadding: 0
                    rightPadding: 0
                    tip: qsTr("Cerrar")
                    Accessible.name: qsTr("Cerrar")
                    onClicked: details.close()
                    Sym {
                        anchors.centerIn: parent
                        name: "x"
                        size: 15
                        color: Theme.dim
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: detailRows.height
                radius: 10
                color: Theme.field
                Column {
                    id: detailRows
                    width: parent.width
                    SettingRow {
                        label: qsTr("Nombre")
                        TField {
                            id: deviceName
                            implicitWidth: 200
                            text: win.shownDevice.name ?? ""
                            maximumLength: 64
                            Accessible.name: qsTr("Nombre del dispositivo")
                            onEditingFinished: {
                                Bt.renameDevice(win.shown, text)
                                text = Qt.binding(() => win.shownDevice.name ?? "")
                            }
                        }
                    }
                    Line {}
                    SettingRow {
                        label: qsTr("Conectar automáticamente")
                        note: qsTr("Puede conectarse a este equipo sin preguntar.")
                        TSwitch {
                            enabled: !win.shownDevice.blocked
                            on: win.shownDevice.trusted ?? false
                            Accessible.name: qsTr("Conectar automáticamente")
                            onAsked: value => Bt.setTrusted(win.shown, value)
                        }
                    }
                    Line {}
                    SettingRow {
                        label: qsTr("Bloquear")
                        note: qsTr("Rechaza cualquier conexión de este dispositivo.")
                        TSwitch {
                            on: win.shownDevice.blocked ?? false
                            Accessible.name: qsTr("Bloquear")
                            onAsked: value => Bt.setBlocked(win.shown, value)
                        }
                    }
                    Line {
                        visible: batteryRow.visible
                    }
                    SettingRow {
                        id: batteryRow
                        visible: (win.shownDevice.battery ?? -1) >= 0
                        label: qsTr("Batería")
                        Text {
                            text: qsTr("%1 %").arg(win.shownDevice.battery ?? 0)
                            color: Theme.dim
                            font.pixelSize: Theme.body
                        }
                    }
                    Line {}
                    SettingRow {
                        label: qsTr("Dirección")
                        Text {
                            text: win.shown
                            color: Theme.dim
                            font.pixelSize: Theme.body
                            font.family: "monospace"
                        }
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                TButton {
                    id: forgetButton
                    /// The first press asks, the second one forgets.
                    property bool armed: false
                    destructive: true
                    text: armed ? qsTr("Sí, olvidarlo") : qsTr("Olvidar este dispositivo")
                    onClicked: {
                        if (!armed) {
                            armed = true
                            return
                        }
                        Bt.forget(win.shown)
                        details.close()
                    }
                }
                Item { Layout.fillWidth: true }
                TButton {
                    visible: !win.shownDevice.blocked
                    enabled: !win.shownDevice.busy
                    primary: !win.shownDevice.connected
                    text: win.shownDevice.connected ? qsTr("Desconectar") : qsTr("Conectar")
                    onClicked: win.shownDevice.connected ? Bt.disconnectFrom(win.shown) : Bt.connectTo(win.shown)
                }
            }
            Text {
                visible: forgetButton.armed
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.dim
                font.pixelSize: Theme.small
                text: qsTr("Para volver a usarlo habrá que enlazarlo de nuevo.")
            }
        }
    }

    // --- A device asks something while pairing -----------------------------------

    Sheet {
        id: pairing
        closePolicy: Popup.CloseOnEscape
        // Closed any way but by its buttons, the answer is no.
        onClosed: {
            if (win.asking)
                Bt.answer(false)
        }

        readonly property string kind: win.request.kind ?? ""
        readonly property bool typed: kind === "pin" || kind === "passkey"

        function accept() {
            if (typed && answerField.text.trim() === "")
                return
            Bt.answer(true, answerField.text.trim())
        }

        contentItem: Column {
            spacing: 12

            Sym {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "bluetooth"
                size: 30
                color: Theme.accent
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                text: pairing.kind === "authorize" ? qsTr("¿Permitir la conexión de «%1»?").arg(win.request.name ?? "")
                                                   : qsTr("Enlazar con «%1»").arg(win.request.name ?? "")
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.dim
                font.pixelSize: Theme.body
                text: pairing.kind === "confirm" ? qsTr("Comprueba que el dispositivo muestra este mismo código.")
                    : pairing.kind === "show" ? qsTr("Escribe este código en el dispositivo y pulsa Intro.")
                    : pairing.kind === "pin" ? qsTr("Escribe el PIN del dispositivo. Suele venir en su manual; a menudo es 0000 o 1234.")
                    : pairing.kind === "passkey" ? qsTr("Escribe el código de 6 cifras que muestra el dispositivo.")
                    : qsTr("Quiere conectarse a este equipo.")
            }
            Text {
                visible: pairing.kind === "confirm" || pairing.kind === "show"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.pixelSize: 34
                font.weight: Font.DemiBold
                font.family: "monospace"
                font.letterSpacing: 2
                text: win.request.code ?? ""
                Accessible.name: qsTr("Código %1").arg(text)
            }
            TField {
                id: answerField
                visible: pairing.typed
                anchors.horizontalCenter: parent.horizontalCenter
                width: 180
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 18
                implicitHeight: 38
                maximumLength: pairing.kind === "passkey" ? 6 : 16
                inputMethodHints: pairing.kind === "passkey" ? Qt.ImhDigitsOnly : Qt.ImhNone
                validator: RegularExpressionValidator {
                    regularExpression: pairing.kind === "passkey" ? /[0-9]{0,6}/ : /.{0,16}/
                }
                Accessible.name: qsTr("Código")
                onAccepted: pairing.accept()
                onVisibleChanged: if (visible) forceActiveFocus()
            }
            Item { width: 1; height: 2 }
            RowLayout {
                width: parent.width
                spacing: 8
                TButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: pairing.kind === "authorize" ? qsTr("Rechazar") : qsTr("Cancelar")
                    onClicked: Bt.answer(false)
                }
                TButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: pairing.kind !== "show"
                    enabled: !pairing.typed || answerField.text.trim() !== ""
                    primary: true
                    text: pairing.kind === "authorize" ? qsTr("Permitir") : qsTr("Enlazar")
                    onClicked: pairing.accept()
                }
            }
        }
    }

    // --- Pictures for the tests (--shot, --do) -----------------------------------

    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        switch (verb) {
        case "details":
            showDetails(arg)
            break
        case "arm":
            forgetButton.armed = true
            break
        case "sim":
            if (Fake)
                Fake.simulate(arg)
            break
        case "connect":
            Bt.connectTo(arg)
            break
        case "visible":
            Bt.setDiscoverable(true)
            break
        case "end":
            flick.contentY = Math.max(0, flick.contentHeight - flick.height)
            break
        default:
            console.warn("acción desconocida:", action)
        }
    }

    Timer {
        id: testStep
        interval: 700
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
