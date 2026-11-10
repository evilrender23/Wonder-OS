import QtQuick
import QtQuick.Controls.Basic

/// The copies each manual save left: listed by date, looked into, gone back to.
Popup {
    id: panel

    property var list: []
    property int current: -1
    readonly property var entry: list[current] ?? null
    readonly property var inside: entry ? App.describeVersion(entry.path) : ({})

    function reload() {
        list = App.versions()
        current = list.length > 0 ? Math.max(0, Math.min(current, list.length - 1)) : -1
    }
    function show() {
        current = 0
        reload()
        open()
    }
    function size(bytes) {
        return bytes >= 1048576 ? (bytes / 1048576).toLocaleString(Qt.locale("es_ES"), "f", 1) + " MB" : Math.max(1, Math.round(bytes / 1024)) + " kB"
    }
    function restore() {
        if (!entry)
            return
        const error = App.restoreVersion(entry.path)
        if (error !== "") {
            problem.text = error
            return
        }
        close()
        App.said("Versión restaurada. Lo que había se ha guardado como otra versión.")
    }
    function act(arg) {
        const colon = arg.indexOf(":")
        const verb = colon < 0 ? arg : arg.slice(0, colon)
        switch (verb) {
        case "pick":
            current = Number(arg.slice(colon + 1))
            break
        case "restore":
            restore()
            break
        case "remove":
            if (entry && App.removeVersion(entry.path))
                reload()
            break
        case "save":
            App.saveVersion()
            reload()
            break
        case "close":
            close()
            break
        }
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 680)
    height: Math.min(parent.height - 60, 400)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#40000000"
    }
    Connections {
        target: App
        enabled: panel.visible
        function onVersionsChanged() { panel.reload() }
    }
    contentItem: Item {
        Text {
            x: 20
            y: 16
            text: "Versiones"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            x: 20
            y: 38
            width: parent.width - 220
            text: "Cada vez que guardas (⌃S) queda una copia entera; se conservan las 20 últimas."
            color: Theme.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        TextButton {
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 18
            symbol: "save"
            text: "Guardar versión ahora"
            onClicked: panel.act("save")
        }
        Rectangle {
            y: 66
            width: parent.width
            height: 1
            color: Theme.border
        }
        ListView {
            id: versions
            x: 8
            y: 74
            width: 250
            height: parent.height - 82
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.list
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool on: index === panel.current
                width: versions.width
                height: 44
                radius: 8
                color: on ? Theme.selection : (rowArea.containsMouse ? Theme.hover : "transparent")
                Sym {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "history"
                    size: 15
                    color: row.on ? Theme.tint : Theme.dim
                }
                Text {
                    x: 34
                    y: 7
                    width: parent.width - 42
                    text: row.modelData.label
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: row.on ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    x: 34
                    y: 25
                    text: panel.size(row.modelData.size) + (row.index === 0 ? " · la más reciente" : "")
                    color: Theme.faint
                    font.pixelSize: 11
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.current = row.index
                }
            }
        }
        Rectangle {
            x: 266
            y: 67
            width: 1
            height: parent.height - 67
            color: Theme.line
        }
        Item {
            x: 267
            y: 67
            width: parent.width - 267
            height: parent.height - 67
            visible: !!panel.entry
            Text {
                id: when
                x: 20
                y: 16
                width: parent.width - 40
                text: panel.entry ? panel.entry.label : ""
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                id: summary
                x: 20
                y: 38
                width: parent.width - 40
                text: panel.inside.ok === false ? (panel.inside.error || "No se puede leer esta copia.")
                    : (panel.inside.tables ?? []).length + " tablas · " + Number(panel.inside.rows ?? 0).toLocaleString(Qt.locale("es_ES"), "f", 0) + " registros · " + (panel.inside.objects ?? 0) + " consultas, formularios e informes"
                color: Theme.dim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            ListView {
                id: inside
                x: 20
                y: 70
                width: parent.width - 36
                height: parent.height - 70 - 58
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: panel.inside.tables ?? []
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    required property var modelData
                    width: inside.width
                    height: 26
                    Sym {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "table"
                        size: 13
                        color: Theme.faint
                    }
                    Text {
                        x: 22
                        width: parent.width - 110
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name
                        color: Theme.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: Number(modelData.rows).toLocaleString(Qt.locale("es_ES"), "f", 0)
                        color: Theme.dim
                        font.pixelSize: 12
                    }
                }
            }
            Text {
                id: problem
                x: 20
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 54
                width: parent.width - 40
                color: Theme.tint
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                spacing: 8
                DialogButton {
                    text: "Eliminar"
                    onClicked: panel.act("remove")
                }
                DialogButton {
                    text: "Cerrar"
                    onClicked: panel.close()
                }
                DialogButton {
                    text: "Restaurar"
                    primary: true
                    active: panel.inside.ok !== false && !App.db.readOnly
                    onClicked: panel.restore()
                }
            }
        }
        Column {
            x: 267
            width: parent.width - 267
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 30
            visible: !panel.entry
            spacing: 8
            Sym {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "history"
                size: 28
                color: Theme.faint
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Aún no hay versiones de esta base"
                color: Theme.dim
                font.pixelSize: 13
            }
        }
    }
}
