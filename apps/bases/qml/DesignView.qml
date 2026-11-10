import QtQuick
import QtQuick.Controls.Basic

/// The design of a table: its fields, keys and indexes, and the DDL that
/// saving will run.
Item {
    id: view

    /// The table being altered; empty for a new one.
    property string oldName
    property var def: ({name: "", columns: [], compositeKeys: [], withoutRowid: false})
    /// Goes up when fields are added, removed or moved: the rows are read again.
    property int shape: 0
    /// Goes up with every change.
    property int rev: 0
    property int picked: 0
    property bool dirty: false
    property string problem
    property bool inspectorOpen: true
    readonly property var column: {
        shape
        return def.columns[picked] ?? null
    }
    readonly property var plan: {
        rev
        return App.checkTable(def) === "" ? App.planTable(oldName, def) : []
    }
    signal saved(string name)
    signal said(string message)
    signal confirm(string title, string text, string action, var callback)

    function blank(name, type) {
        return {name: name, type: type, notNull: false, pk: 0, unique: false, autoIncrement: false, defaultValue: "", origin: "",
            fkTable: "", fkColumn: "", fkOnDelete: "", fkOnUpdate: ""}
    }
    function load(table) {
        oldName = table
        if (table !== "") {
            // A copy of its own: what is typed changes it in place.
            def = JSON.parse(JSON.stringify(App.tableDef(table)))
        } else {
            const id = blank("id", "INTEGER")
            id.pk = 1
            def = {name: "", columns: [id, blank("nombre", "TEXT")], compositeKeys: [], withoutRowid: false}
        }
        picked = 0
        problem = ""
        dirty = false
        shape++
        rev++
    }
    function touch(structural) {
        dirty = true
        problem = ""
        if (structural)
            shape++
        rev++
    }
    function set(index, key, value) {
        if (!def.columns[index] || def.columns[index][key] === value)
            return
        def.columns[index][key] = value
        touch(key !== "name" && key !== "defaultValue")
    }
    function setKey(index, on) {
        const columns = def.columns
        if (on) {
            let last = 0
            for (const c of columns)
                last = Math.max(last, c.pk)
            columns[index].pk = last + 1
        } else {
            const was = columns[index].pk
            columns[index].pk = 0
            columns[index].autoIncrement = false
            for (const c of columns)
                if (c.pk > was)
                    c.pk--
        }
        touch(true)
    }
    function add() {
        let n = def.columns.length + 1
        const names = def.columns.map(c => c.name.toLowerCase())
        while (names.includes("campo" + n))
            n++
        def.columns.push(blank("campo" + n, "TEXT"))
        picked = def.columns.length - 1
        touch(true)
    }
    function remove(index) {
        if (def.columns.length <= 1)
            return
        const was = def.columns[index].pk
        def.columns.splice(index, 1)
        if (was > 0)
            for (const c of def.columns)
                if (c.pk > was)
                    c.pk--
        picked = Math.min(picked, def.columns.length - 1)
        touch(true)
    }
    function move(index, by) {
        const to = index + by
        if (to < 0 || to >= def.columns.length)
            return
        const [moved] = def.columns.splice(index, 1)
        def.columns.splice(to, 0, moved)
        picked = to
        touch(true)
    }
    function save() {
        def.name = nameField.text.trim()
        const wrong = App.checkTable(def)
        if (wrong !== "") {
            problem = wrong
            return
        }
        if (oldName !== "" && plan.length === 0) {
            dirty = false
            return
        }
        const losses = oldName !== "" && plan.length > 1 ? App.tableLosses(oldName) : []
        const go = () => {
            const error = App.saveTable(oldName, def)
            if (error !== "") {
                problem = error
                return
            }
            const name = def.name
            said(oldName === "" ? "Tabla «" + name + "» creada." : "Tabla «" + name + "» guardada.")
            load(name)
            saved(name)
        }
        if (losses.length > 0)
            confirm("Guardar rehace la tabla", "Los registros se conservan, pero esto no se puede trasladar:\n• " + losses.join("\n• "), "Guardar", go)
        else
            go()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.window
    }

    Item {
        id: sheet
        anchors.left: parent.left
        anchors.right: side.visible ? side.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Row {
            id: top
            x: 20
            y: 14
            width: parent.width - 40
            spacing: 10
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Nombre de la tabla"
                color: Theme.dim
                font.pixelSize: 13
            }
            Field {
                id: nameField
                width: Math.min(260, parent.width - 360)
                text: {
                    view.shape
                    return view.def.name
                }
                placeholderText: "clientes"
                onTextEdited: {
                    view.def.name = text
                    view.touch(false)
                }
            }
            Item {
                width: parent.width - nameField.width - 130 - saveRow.width - 20
                height: 1
            }
            Row {
                id: saveRow
                spacing: 8
                DialogButton {
                    text: "Descartar"
                    visible: view.oldName !== ""
                    active: view.dirty
                    onClicked: view.load(view.oldName)
                }
                DialogButton {
                    text: view.oldName === "" ? "Crear tabla" : "Guardar cambios"
                    primary: true
                    active: view.dirty || view.oldName === ""
                    onClicked: view.save()
                }
            }
        }
        Text {
            id: wrong
            anchors.top: top.bottom
            anchors.topMargin: 8
            x: 20
            width: parent.width - 40
            visible: view.problem !== ""
            height: visible ? implicitHeight : 0
            text: view.problem
            color: Theme.tint
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        // The headings of the columns of the list.
        Item {
            id: heads
            anchors.top: wrong.bottom
            anchors.topMargin: 12
            x: 20
            width: parent.width - 40
            height: 22
            readonly property real nameWidth: Math.max(110, width - 30 - 140 - 3 * 58 - 130 - 84 - 7 * 8)
            Row {
                spacing: 8
                Item { width: 30; height: 1 }
                Caption { width: heads.nameWidth; text: "Campo" }
                Caption { width: 140; text: "Tipo" }
                Caption { width: 58; text: "Clave"; horizontalAlignment: Text.AlignHCenter }
                Caption { width: 58; text: "Oblig."; horizontalAlignment: Text.AlignHCenter }
                Caption { width: 58; text: "Único"; horizontalAlignment: Text.AlignHCenter }
                Caption { width: 130; text: "Por omisión" }
            }
        }
        ListView {
            id: list
            anchors.top: heads.bottom
            anchors.bottom: more.top
            anchors.bottomMargin: 6
            x: 12
            width: parent.width - 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: {
                view.shape
                return view.def.columns.length
            }
            delegate: Rectangle {
                id: line
                required property int index
                readonly property var c: {
                    view.shape
                    return view.def.columns[index] ?? view.blank("", "TEXT")
                }
                width: list.width
                height: 36
                radius: 7
                color: view.picked === index ? Theme.selection : (lineArea.containsMouse ? Theme.hover : "transparent")
                MouseArea {
                    id: lineArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: view.picked = line.index
                }
                Row {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Item {
                        width: 30
                        height: 28
                        Sym {
                            anchors.centerIn: parent
                            name: line.c.pk > 0 ? "key" : (line.c.fkTable ? "link" : Theme.kindSymbol(App.kindOf(line.c.type)))
                            size: 14
                            color: line.c.pk > 0 ? Theme.tint : Theme.dim
                        }
                    }
                    Field {
                        width: heads.nameWidth
                        text: line.c.name
                        onTextEdited: view.set(line.index, "name", text)
                        onActiveFocusChanged: if (activeFocus) view.picked = line.index
                    }
                    FCombo {
                        width: 140
                        implicitWidth: 140
                        model: {
                            const names = App.typeNames()
                            const own = line.c.type.toUpperCase()
                            return own === "" || names.includes(own) ? names : names.concat([line.c.type])
                        }
                        currentIndex: Math.max(0, model.findIndex(t => t.toUpperCase() === line.c.type.toUpperCase()))
                        onActivated: {
                            view.picked = line.index
                            view.set(line.index, "type", currentText)
                        }
                    }
                    Item {
                        width: 58
                        height: 28
                        Check {
                            anchors.centerIn: parent
                            checked: line.c.pk > 0
                            onToggled: on => {
                                view.picked = line.index
                                view.setKey(line.index, on)
                            }
                        }
                    }
                    Item {
                        width: 58
                        height: 28
                        Check {
                            anchors.centerIn: parent
                            checked: line.c.notNull
                            onToggled: on => view.set(line.index, "notNull", on)
                        }
                    }
                    Item {
                        width: 58
                        height: 28
                        Check {
                            anchors.centerIn: parent
                            checked: line.c.unique
                            onToggled: on => view.set(line.index, "unique", on)
                        }
                    }
                    Field {
                        width: 130
                        text: line.c.defaultValue
                        placeholderText: "—"
                        onTextEdited: view.set(line.index, "defaultValue", text)
                        onActiveFocusChanged: if (activeFocus) view.picked = line.index
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        TButton {
                            width: 26
                            symbol: "chevron-up"
                            size: 14
                            active: line.index > 0
                            onClicked: view.move(line.index, -1)
                        }
                        TButton {
                            width: 26
                            symbol: "chevron-down"
                            size: 14
                            active: line.index < view.def.columns.length - 1
                            onClicked: view.move(line.index, 1)
                        }
                        TButton {
                            width: 26
                            symbol: "trash-2"
                            size: 14
                            active: view.def.columns.length > 1
                            onClicked: view.remove(line.index)
                        }
                    }
                }
            }
        }
        TextButton {
            id: more
            x: 20
            anchors.bottom: sqlBox.top
            anchors.bottomMargin: 10
            symbol: "plus"
            text: "Añadir campo"
            onClicked: view.add()
        }
        // What saving will run.
        Rectangle {
            id: sqlBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.min(170, Math.max(74, parent.height * 0.26))
            color: Theme.chrome
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Caption {
                x: 20
                y: 10
                text: view.oldName === "" ? "SQL que creará la tabla" : (view.plan.length > 1 ? "SQL que guardar ejecutará (rehace la tabla en una transacción)" : "SQL que guardar ejecutará")
            }
            Flickable {
                anchors.fill: parent
                anchors.topMargin: 30
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                anchors.bottomMargin: 8
                contentHeight: sqlText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                TextEdit {
                    id: sqlText
                    width: parent.width - 12
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    text: {
                        view.rev
                        const wrong = App.checkTable(view.def)
                        if (wrong !== "")
                            return "-- " + wrong
                        return view.plan.length === 0 ? "-- Sin cambios" : view.plan.join(";\n") + ";"
                    }
                    color: Theme.dim
                    font.family: Theme.mono
                    font.pixelSize: 12
                    Highlighter {
                        document: sqlText.textDocument
                        dark: Theme.dark
                    }
                }
            }
        }
    }

    Inspector {
        id: side
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        visible: view.inspectorOpen
        heading: view.column ? "Campo «" + view.column.name + "»" : "Campo"
        readonly property var c: view.column ?? view.blank("", "TEXT")
        readonly property var tables: {
            App.db.schemaRevision
            return App.db.tableNames(false)
        }
        readonly property var actions: ["", "CASCADE", "SET NULL", "SET DEFAULT", "RESTRICT", "NO ACTION"]
        readonly property var actionNames: ["Sin acción declarada", "En cascada (CASCADE)", "Dejar vacío (SET NULL)", "Valor por omisión (SET DEFAULT)", "Impedirlo (RESTRICT)", "Nada (NO ACTION)"]

        Check {
            width: parent.width
            text: "Se numera solo"
            checked: side.c.autoIncrement
            active: side.c.pk > 0 && side.c.type.toUpperCase() === "INTEGER" && view.def.columns.filter(c => c.pk > 0).length === 1
            onToggled: on => view.set(view.picked, "autoIncrement", on)
        }
        Item { width: 1; height: 4 }
        Caption { text: "Clave foránea" }
        Text {
            width: parent.width
            text: "Tabla a la que se refiere"
            color: Theme.dim
            font.pixelSize: 12
        }
        FCombo {
            width: parent.width
            model: ["(ninguna)"].concat(side.tables)
            currentIndex: Math.max(0, side.tables.indexOf(side.c.fkTable) + 1)
            onActivated: index => {
                const table = index === 0 ? "" : side.tables[index - 1]
                view.set(view.picked, "fkTable", table)
                let target = ""
                if (table !== "") {
                    const columns = table === view.oldName ? view.def.columns : App.db.columns(table)
                    const key = columns.find(c => c.pk > 0)
                    target = key ? key.name : (columns[0]?.name ?? "")
                }
                view.set(view.picked, "fkColumn", target)
            }
        }
        Text {
            width: parent.width
            visible: side.c.fkTable !== ""
            text: "Campo de esa tabla"
            color: Theme.dim
            font.pixelSize: 12
        }
        FCombo {
            id: fkColumn
            width: parent.width
            visible: side.c.fkTable !== ""
            model: side.c.fkTable === "" ? [] : App.db.columnNames(side.c.fkTable)
            currentIndex: Math.max(0, model.indexOf(side.c.fkColumn))
            onActivated: view.set(view.picked, "fkColumn", currentText)
        }
        Text {
            width: parent.width
            visible: side.c.fkTable !== ""
            text: "Al borrar el registro referido"
            color: Theme.dim
            font.pixelSize: 12
        }
        FCombo {
            width: parent.width
            visible: side.c.fkTable !== ""
            model: side.actionNames
            currentIndex: Math.max(0, side.actions.indexOf(side.c.fkOnDelete.toUpperCase()))
            onActivated: index => view.set(view.picked, "fkOnDelete", side.actions[index])
        }
        Text {
            width: parent.width
            visible: side.c.fkTable !== ""
            text: "Al cambiar su clave"
            color: Theme.dim
            font.pixelSize: 12
        }
        FCombo {
            width: parent.width
            visible: side.c.fkTable !== ""
            model: side.actionNames
            currentIndex: Math.max(0, side.actions.indexOf(side.c.fkOnUpdate.toUpperCase()))
            onActivated: index => view.set(view.picked, "fkOnUpdate", side.actions[index])
        }
        Text {
            width: parent.width
            visible: view.def.compositeKeys.length > 0
            text: view.def.compositeKeys.length === 1 ? "La tabla tiene además una clave foránea de varios campos; se conserva tal cual."
                : "La tabla tiene además " + view.def.compositeKeys.length + " claves foráneas de varios campos; se conservan tal cual."
            color: Theme.faint
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        Item { width: 1; height: 6 }
        Caption {
            text: "Índices"
            visible: view.oldName !== ""
        }
        Repeater {
            model: {
                App.db.schemaRevision
                return view.oldName === "" ? [] : App.db.indexes(view.oldName)
            }
            Item {
                id: indexLine
                required property var modelData
                width: parent.width
                height: 34
                Text {
                    y: 1
                    width: parent.width - 30
                    text: indexLine.modelData.name
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
                Text {
                    y: 17
                    width: parent.width - 30
                    text: (indexLine.modelData.unique ? "Único · " : "") + indexLine.modelData.columns.join(", ")
                    color: Theme.faint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                TButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    visible: indexLine.modelData.origin === "c"
                    symbol: "trash-2"
                    size: 13
                    onClicked: {
                        const error = App.dropIndex(indexLine.modelData.name)
                        if (error !== "")
                            view.said(error)
                    }
                }
            }
        }
        TextButton {
            visible: view.oldName !== "" && view.column !== null && view.column.origin !== ""
            symbol: "plus"
            text: "Índice sobre este campo"
            onClicked: {
                const name = view.column.origin
                let index = "idx_" + view.oldName + "_" + name
                const taken = App.db.indexes(view.oldName).map(i => i.name)
                for (let n = 2; taken.includes(index); ++n)
                    index = "idx_" + view.oldName + "_" + name + "_" + n
                const error = App.createIndex(view.oldName, index, [name], false)
                view.said(error !== "" ? error : "Índice «" + index + "» creado.")
            }
        }
        Text {
            width: parent.width
            visible: view.oldName !== "" && view.dirty
            text: "Los índices se crean sobre la tabla guardada."
            color: Theme.faint
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }
}
