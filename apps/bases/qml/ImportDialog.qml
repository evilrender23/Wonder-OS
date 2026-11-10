import QtQuick
import QtQuick.Controls.Basic

/// Importing a CSV file: what was detected (separator, header, the type of
/// each column) is shown over the first rows and can be changed before it goes in.
Popup {
    id: dialog

    property string path
    property string separator
    property int header: -1
    /// {ok, error, separator, header, columns: [{name, type}], rows, count}
    property var preview: ({})
    /// What a person changed: [{name, type, skip}]
    property var columns: []
    property string problem
    readonly property var types: ["TEXT", "INTEGER", "REAL", "DATE", "DATETIME", "BOOLEAN"]
    readonly property var separators: [{id: "", label: "Detectar"}, {id: ",", label: "Coma  ,"}, {id: ";", label: "Punto y coma  ;"}, {id: "\t", label: "Tabulador"}, {id: "|", label: "Barra  |"}]
    readonly property int columnWidth: 132
    signal done(string table)

    function read() {
        preview = App.previewCsv(path, separator, header)
        columns = (preview.columns ?? []).map(c => ({name: c.name, type: c.type, skip: false}))
        problem = preview.ok === false ? (preview.error || "No se puede leer el archivo.") : ""
    }
    function show(file) {
        path = file
        separator = ""
        header = -1
        read()
        let base = file.slice(file.lastIndexOf("/") + 1).replace(/\.\w+$/, "").toLowerCase().replace(/[^a-z0-9áéíóúñü_]+/g, "_").replace(/^_+|_+$/g, "")
        if (base === "" || /^\d/.test(base))
            base = "datos_" + base
        let name = base
        for (let n = 2; App.db.exists(name); ++n)
            name = base + "_" + n
        nameField.text = name
        open()
        nameField.forceActiveFocus()
    }
    function change(index, what, value) {
        const next = columns.slice()
        next[index] = Object.assign({}, next[index])
        next[index][what] = value
        columns = next
    }
    function go() {
        const table = nameField.text.trim()
        if (table === "") {
            problem = "La tabla necesita un nombre."
            return
        }
        if (App.db.exists(table)) {
            problem = "Ya hay una tabla «" + table + "»."
            return
        }
        if (columns.every(c => c.skip)) {
            problem = "No queda ninguna columna que importar."
            return
        }
        const result = App.importCsv(path, table, {separator: preview.separator ?? separator, header: preview.header ? 1 : 0, columns: columns})
        if (!result.ok) {
            problem = result.error || "No se pudo importar."
            return
        }
        close()
        App.said(Number(result.rows).toLocaleString(Qt.locale("es_ES"), "f", 0) + " registros importados en «" + result.table + "».")
        done(result.table)
    }
    function act(arg) {
        const colon = arg.indexOf(":")
        const verb = colon < 0 ? arg : arg.slice(0, colon)
        const rest = colon < 0 ? "" : arg.slice(colon + 1)
        switch (verb) {
        case "name":
            nameField.text = rest
            break
        case "sep":
            separator = rest === "tab" ? "\t" : rest
            read()
            break
        case "header":
            header = Number(rest)
            read()
            break
        case "type": {
            const at = rest.indexOf("=")
            const index = columns.findIndex(c => c.name === rest.slice(0, at))
            if (index >= 0)
                change(index, "type", rest.slice(at + 1))
            break
        }
        case "skip": {
            const index = columns.findIndex(c => c.name === rest)
            if (index >= 0)
                change(index, "skip", true)
            break
        }
        case "cancel":
            close()
            break
        default:
            go()
        }
    }

    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 760)
    height: Math.min(parent.height - 60, 500)
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
    contentItem: Item {
        Text {
            id: heading
            text: "Importar CSV"
            color: Theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            y: 24
            width: parent.width
            text: dialog.path.slice(dialog.path.lastIndexOf("/") + 1) + " · " + Number(dialog.preview.count ?? 0).toLocaleString(Qt.locale("es_ES"), "f", 0) + " registros"
            color: Theme.dim
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
        Row {
            id: options
            y: 54
            spacing: 10
            Column {
                spacing: 4
                Caption { text: "Tabla nueva" }
                Field {
                    id: nameField
                    width: 210
                    onAccepted: dialog.go()
                }
            }
            Column {
                spacing: 4
                Caption { text: "Separador" }
                FCombo {
                    width: 170
                    model: dialog.separators.map(s => s.label)
                    currentIndex: Math.max(0, dialog.separators.findIndex(s => s.id === dialog.separator))
                    onActivated: index => {
                        dialog.separator = dialog.separators[index].id
                        dialog.read()
                    }
                }
            }
            Column {
                spacing: 4
                Caption { text: " " }
                Item {
                    width: 260
                    height: 28
                    Check {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "La primera fila son los nombres"
                        checked: !!dialog.preview.header
                        onToggled: on => {
                            dialog.header = on ? 1 : 0
                            dialog.read()
                        }
                    }
                }
            }
        }
        Rectangle {
            id: sheet
            y: 116
            width: parent.width
            height: parent.height - 116 - 48
            radius: 8
            color: Theme.dark ? "#1c1c1e" : "#ffffff"
            border.color: Theme.border
            clip: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 1
                contentWidth: Math.max(width, dialog.columns.length * dialog.columnWidth)
                contentHeight: heads.height + lines.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}
                Row {
                    id: heads
                    Repeater {
                        model: dialog.columns
                        Rectangle {
                            id: head
                            required property var modelData
                            required property int index
                            width: dialog.columnWidth
                            height: 66
                            color: Theme.dark ? "#2c2c2e" : "#f5f5f7"
                            opacity: modelData.skip ? 0.45 : 1
                            Check {
                                x: 8
                                y: 7
                                width: parent.width - 14
                                text: head.modelData.name
                                checked: !head.modelData.skip
                                onToggled: on => dialog.change(head.index, "skip", !on)
                            }
                            FCombo {
                                x: 6
                                y: 32
                                width: parent.width - 12
                                model: dialog.types
                                currentIndex: Math.max(0, dialog.types.indexOf(head.modelData.type))
                                onActivated: index => dialog.change(head.index, "type", dialog.types[index])
                            }
                            Rectangle {
                                anchors.right: parent.right
                                width: 1
                                height: parent.height
                                color: Theme.line
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: Theme.border
                            }
                        }
                    }
                }
                Column {
                    id: lines
                    y: heads.height
                    Repeater {
                        model: dialog.preview.rows ?? []
                        Row {
                            id: line
                            required property var modelData
                            Repeater {
                                model: dialog.columns
                                Item {
                                    required property var modelData
                                    required property int index
                                    width: dialog.columnWidth
                                    height: 24
                                    opacity: modelData.skip ? 0.35 : 1
                                    Text {
                                        x: 9
                                        width: parent.width - 16
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: modelData.type === "INTEGER" || modelData.type === "REAL" ? Text.AlignRight : Text.AlignLeft
                                        text: line.modelData[index] ?? ""
                                        color: Theme.text
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: Theme.line
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Text {
            anchors.left: parent.left
            anchors.right: buttons.left
            anchors.rightMargin: 12
            anchors.verticalCenter: buttons.verticalCenter
            text: dialog.problem !== "" ? dialog.problem : "Se muestran las primeras filas. Los tipos se han deducido de todos los valores."
            color: dialog.problem !== "" ? Theme.tint : Theme.faint
            font.pixelSize: 12
            elide: Text.ElideRight
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
                text: "Importar"
                primary: true
                active: dialog.preview.ok !== false
                onClicked: dialog.go()
            }
        }
    }
}
