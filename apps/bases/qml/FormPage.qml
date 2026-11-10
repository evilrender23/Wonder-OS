import QtQuick
import QtQuick.Controls.Basic
import WonderlandBases

/// A form: the records of a table one at a time, with only the fields wanted,
/// and below the rows of the tables that refer to the record in sight.
Item {
    id: page

    property var host
    property string key
    property string name
    property int objectId
    /// {table, fields: [{name, label, show}], subforms: [{table, column, show}]}
    property var definition: ({table: "", fields: [], subforms: []})
    property string saved: "{}"
    property int current: 0
    property int subIndex: 0
    readonly property bool inspectorOpen: host ? host.inspectorOpen : false
    readonly property bool dirty: JSON.stringify(definition) !== saved
    readonly property var shownFields: (definition.fields ?? []).filter(f => f.show !== false).map(f => f.name)
    readonly property var subforms: (definition.subforms ?? []).filter(s => s.show !== false)
    readonly property var record: {
        App.db.dataRevision
        return rows.count > 0 && current >= 0 && current < rows.count ? rows.record(current) : ({})
    }
    readonly property var keyValue: {
        const pk = rows.fields.find(f => f.pk > 0)
        return pk ? record[pk.name] : undefined
    }
    readonly property string status: rows.count === 0 ? "Sin registros" : "Registro " + host.count(current + 1) + " de " + host.count(rows.count)
    readonly property string detail: definition.table ? "Formulario de " + definition.table : ""

    Component.onCompleted: {
        if (objectId > 0) {
            definition = App.db.object(objectId).definition ?? definition
            saved = JSON.stringify(definition)
        }
    }
    function apply(extra) {
        if (extra.source && objectId === 0 && !definition.table)
            definition = App.defaultForm(extra.source)
    }
    function change(list, index, what, value) {
        const next = JSON.parse(JSON.stringify(definition))
        next[list][index][what] = value
        definition = next
        subIndex = Math.min(subIndex, Math.max(0, subforms.length - 1))
    }
    function save(given) {
        const store = title => {
            const id = App.db.saveObject("form", title, definition, objectId)
            if (id === 0) {
                host.say(App.db.lastError || "No se pudo guardar el formulario.")
                return
            }
            saved = JSON.stringify(definition)
            host.objectSaved(key, "form", id, title)
        }
        if (given)
            store(given)
        else if (objectId > 0)
            store(name)
        else
            host.askName("Guardar el formulario", "Aparecerá en la barra lateral, bajo Formularios.", definition.table ? "Formulario de " + definition.table : "", "Guardar", store)
    }
    function act(verb, arg) {
        switch (verb) {
        case "save":
            save(arg)
            return true
        case "go":
            form.go(Number(arg))
            return true
        case "put": {
            const at = arg.indexOf("=")
            return form.put(arg.slice(0, at), arg.slice(at + 1))
        }
        case "hide":
        case "showfield": {
            const at = definition.fields.findIndex(f => f.name === arg)
            if (at >= 0)
                change("fields", at, "show", verb !== "hide")
            return at >= 0
        }
        case "sub":
            subIndex = Number(arg)
            return true
        case "addrow":
            add()
            return true
        case "refresh":
            rows.refresh()
            return true
        }
        return false
    }
    function add() {
        const at = rows.insert({})
        if (at === -2)
            host.say(rows.error)
        else if (at >= 0)
            current = at
        else
            current = Math.max(0, rows.count - 1)
    }

    GridModel {
        id: rows
        database: App.db
        table: page.definition.table ?? ""
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 42
        z: 4
        color: Theme.window
        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Sym {
                anchors.verticalCenter: parent.verticalCenter
                name: "form"
                size: 15
                color: Theme.tint
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: page.name !== "" ? page.name : "Formulario nuevo"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: page.definition.table ? "· " + page.definition.table : ""
                color: Theme.dim
                font.pixelSize: 13
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TextButton {
                symbol: "table"
                text: "Ver la tabla"
                active: !!page.definition.table
                onClicked: page.host.openObject("table", page.definition.table)
            }
            TextButton {
                symbol: "save"
                text: page.objectId > 0 ? "Guardar" : "Guardar…"
                strong: page.dirty
                active: page.dirty || page.objectId === 0
                onClicked: page.save("")
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    Item {
        id: body
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: side.visible ? side.left : parent.right
        Rectangle {
            anchors.fill: parent
            color: Theme.window
        }
        RecordForm {
            id: form
            width: parent.width
            height: page.subforms.length > 0 ? Math.round(parent.height * 0.56) : parent.height
            model: rows
            current: page.current
            only: page.shownFields
            labels: {
                const map = {}
                for (const f of page.definition.fields ?? [])
                    map[f.name] = f.label || f.name
                return map
            }
            onMoved: row => page.current = row
            onSaid: message => page.host.say(message)
            onAdded: page.add()
        }
        Rectangle {
            id: subs
            anchors.top: form.bottom
            anchors.bottom: parent.bottom
            width: parent.width
            visible: page.subforms.length > 0
            color: Theme.window
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Row {
                id: subBar
                x: 12
                y: 8
                height: 26
                spacing: 8
                Sym {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "link"
                    size: 13
                    color: Theme.faint
                }
                Segmented {
                    options: page.subforms.map((s, i) => ({key: String(i), label: s.table}))
                    current: String(page.subIndex)
                    onPicked: key => page.subIndex = Number(key)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: subGrid.count === 1 ? "1 registro enlazado" : subGrid.count + " registros enlazados"
                    color: Theme.dim
                    font.pixelSize: 12
                }
            }
            DataGrid {
                id: subGrid
                readonly property var sub: page.subforms[page.subIndex] ?? null
                anchors.fill: parent
                anchors.topMargin: 42
                compact: true
                table: sub ? sub.table : ""
                linkColumn: sub ? sub.column : ""
                linkValue: page.keyValue
                onSaid: message => page.host.say(message)
            }
        }
    }

    Inspector {
        id: side
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: page.inspectorOpen
        heading: "Diseño del formulario"
        Caption { text: "Campos" }
        Repeater {
            model: page.definition.fields ?? []
            Check {
                required property var modelData
                required property int index
                width: parent.width
                text: modelData.label || modelData.name
                checked: modelData.show !== false
                onToggled: on => page.change("fields", index, "show", on)
            }
        }
        Item { width: 1; height: 6 }
        Caption {
            visible: (page.definition.subforms ?? []).length > 0
            text: "Subformularios"
        }
        Repeater {
            model: page.definition.subforms ?? []
            Check {
                required property var modelData
                required property int index
                width: parent.width
                text: modelData.table + " · " + modelData.column
                checked: modelData.show !== false
                onToggled: on => page.change("subforms", index, "show", on)
            }
        }
    }
}
