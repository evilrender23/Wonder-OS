import QtQuick
import QtQuick.Controls.Basic

/// One record at a time, a field per line, with the way to the next and the last.
Item {
    id: form

    property var model
    property int current: -1
    /// The fields in sight, by name; empty for all of them.
    property var only: []
    /// name → what to call it; the name itself when missing.
    property var labels: ({})
    readonly property bool editable: !!model && model.editable
    readonly property var record: {
        App.db.dataRevision
        if (!model || current < 0 || current >= model.count)
            return ({})
        return model.record(current)
    }
    readonly property var fields: {
        if (!model)
            return []
        return model.fields.map((f, i) => Object.assign({at: i}, f)).filter(f => only.length === 0 || only.includes(f.name))
    }
    signal moved(int row)
    signal said(string message)
    signal added()

    function go(row) {
        if (model && model.count > 0)
            moved(Math.max(0, Math.min(model.count - 1, row)))
    }
    /// Stores what was typed in the field of that name.
    function put(name, text) {
        const at = model.fields.findIndex(f => f.name === name)
        if (at < 0 || current < 0 || !editable)
            return false
        const ok = text === null ? model.setNull(current, at) : model.setValue(current, at, text)
        if (!ok)
            said(model.error)
        return ok
    }

    Flickable {
        anchors.fill: parent
        anchors.bottomMargin: 44
        contentHeight: sheet.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Rectangle {
            id: sheet
            x: Math.max(20, (parent.width - width) / 2)
            y: 20
            width: Math.min(620, form.width - 40)
            height: lines.height + 36
            radius: 12
            color: Theme.rest
            border.color: Theme.line
            visible: form.current >= 0
            Column {
                id: lines
                x: 20
                y: 18
                width: parent.width - 40
                spacing: 10
                Repeater {
                    model: form.fields
                    Item {
                        id: line
                        required property var modelData
                        readonly property var value: form.record[modelData.name]
                        readonly property bool empty: value === null || value === undefined
                        readonly property bool automatic: modelData.pk > 0 && modelData.kind === "integer" || !!modelData.generated
                        readonly property string referred: {
                            if (!modelData.fkTable || empty)
                                return ""
                            App.db.dataRevision
                            const rows = App.db.related(form.model.table, form.record, 1)
                            const hit = rows.find(r => r.direction === "parent" && r.refColumn === modelData.name)
                            return hit && hit.rows.length > 0 ? hit.rows[0].label : ""
                        }
                        width: lines.width
                        height: 30
                        Row {
                            width: 168
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Sym {
                                anchors.verticalCenter: parent.verticalCenter
                                name: line.modelData.pk > 0 ? "key" : (line.modelData.fkTable ? "link" : Theme.kindSymbol(line.modelData.kind))
                                size: 12
                                color: Theme.faint
                            }
                            Text {
                                width: 146
                                text: (form.labels[line.modelData.name] ?? line.modelData.name) + (line.modelData.notNull && !line.automatic ? " *" : "")
                                color: Theme.dim
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                        Check {
                            x: 176
                            anchors.verticalCenter: parent.verticalCenter
                            visible: line.modelData.kind === "bool"
                            checked: !line.empty && Number(line.value) !== 0
                            active: form.editable
                            onToggled: on => form.put(line.modelData.name, on ? 1 : 0)
                        }
                        Field {
                            x: 176
                            width: parent.width - 176 - (hint.visible ? hint.width + 8 : 0)
                            visible: line.modelData.kind !== "bool"
                            readOnly: !form.editable || line.automatic || line.modelData.kind === "blob"
                            text: line.empty ? "" : form.model.display(line.value)
                            placeholderText: line.automatic ? "automático" : "NULL"
                            font.family: line.modelData.kind === "integer" || line.modelData.kind === "real" ? Theme.mono : font.family
                            onEditingFinished: {
                                if (readOnly || text === (line.empty ? "" : form.model.display(line.value)))
                                    return
                                if (!form.put(line.modelData.name, text === "" ? null : text))
                                    text = line.empty ? "" : form.model.display(line.value)
                            }
                        }
                        Text {
                            id: hint
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: line.referred !== ""
                            width: Math.min(implicitWidth, 150)
                            text: "→ " + line.referred
                            color: Theme.tint
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 80
            visible: form.current < 0
            text: "Sin registros"
            color: Theme.faint
            font.pixelSize: 14
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 44
        color: Theme.window
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }
        Row {
            anchors.centerIn: parent
            spacing: 4
            TButton {
                symbol: "chevrons-left"
                active: form.current > 0
                onClicked: form.go(0)
            }
            TButton {
                symbol: "chevron-left"
                active: form.current > 0
                onClicked: form.go(form.current - 1)
            }
            Text {
                width: 150
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: form.model && form.model.count > 0 ? (form.current + 1) + " de " + form.model.count : "0 de 0"
                color: Theme.dim
                font.pixelSize: 13
            }
            TButton {
                symbol: "chevron-right"
                active: !!form.model && form.current < form.model.count - 1
                onClicked: form.go(form.current + 1)
            }
            TButton {
                symbol: "chevrons-right"
                active: !!form.model && form.current < form.model.count - 1
                onClicked: form.go(form.model.count - 1)
            }
            Item { width: 10; height: 1 }
            TButton {
                symbol: "plus"
                active: form.editable
                onClicked: form.added()
            }
        }
    }
}
