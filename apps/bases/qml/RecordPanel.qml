import QtQuick
import QtQuick.Controls.Basic

/// The record picked, field by field, and what it is tied to by foreign keys:
/// the record each key refers to and the ones that refer to this one.
Inspector {
    id: panel

    property string table
    /// column → value; empty when nothing is picked.
    property var record: ({})
    property var fields: []
    property string activeColumn
    readonly property bool has: Object.keys(record).length > 0
    readonly property var links: {
        App.db.dataRevision
        App.db.schemaRevision
        return has && table !== "" ? App.db.related(table, record, 4) : []
    }
    /// Go to the rows of `table` whose `column` is `value`.
    signal follow(string table, string column, var value)

    function show(value) {
        return value === null || value === undefined ? "NULL" : String(value)
    }

    Text {
        width: parent.width
        visible: !panel.has
        text: "Selecciona un registro para ver sus campos y lo que tiene relacionado."
        color: Theme.faint
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
    Repeater {
        model: panel.has ? panel.fields : []
        Column {
            id: entry
            required property var modelData
            readonly property var cell: panel.record[modelData.name]
            readonly property bool empty: cell === null || cell === undefined
            readonly property bool on: modelData.name === panel.activeColumn
            width: parent.width
            spacing: 2
            Row {
                spacing: 5
                Sym {
                    anchors.verticalCenter: parent.verticalCenter
                    name: entry.modelData.pk > 0 ? "key" : (entry.modelData.fkTable ? "link" : Theme.kindSymbol(entry.modelData.kind))
                    size: 11
                    color: entry.on ? Theme.tint : Theme.faint
                }
                Text {
                    text: entry.modelData.name
                    color: entry.on ? Theme.tint : Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            Text {
                width: parent.width
                text: entry.empty ? "NULL" : (entry.modelData.kind === "bool" ? (Number(entry.cell) ? "Sí" : "No") : String(entry.cell))
                color: entry.empty ? Theme.faint : Theme.text
                font.pixelSize: 13
                font.italic: entry.empty
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
                bottomPadding: 3
            }
        }
    }

    Item {
        width: parent.width
        height: 22
        visible: panel.has
        Rectangle {
            y: 6
            width: parent.width
            height: 1
            color: Theme.border
        }
    }
    Caption {
        visible: panel.has
        text: "Relacionados"
    }
    Text {
        width: parent.width
        visible: panel.has && panel.links.length === 0
        text: "Ninguna clave foránea llega a este registro ni sale de él."
        color: Theme.faint
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
    Repeater {
        model: panel.links
        Column {
            id: link
            required property var modelData
            readonly property bool up: modelData.direction === "parent"
            width: parent.width
            spacing: 2
            Rectangle {
                width: parent.width
                height: 26
                radius: 6
                color: headArea.containsMouse ? Theme.hover : "transparent"
                Sym {
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    name: link.up ? "arrow-right" : "table"
                    size: 13
                    color: Theme.tint
                }
                Text {
                    x: 24
                    width: parent.width - 30 - amount.width - 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: link.modelData.table + (link.up ? "" : " · " + link.modelData.column)
                    color: Theme.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    id: amount
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: link.up ? "" : link.modelData.count
                    color: Theme.dim
                    font.pixelSize: 11
                }
                MouseArea {
                    id: headArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.follow(link.modelData.table, link.modelData.column, link.modelData.value)
                }
            }
            Repeater {
                model: link.modelData.rows
                Text {
                    required property var modelData
                    x: 24
                    width: link.width - 28
                    height: 19
                    verticalAlignment: Text.AlignVCenter
                    text: {
                        const label = modelData.label
                        if (label !== "")
                            return label
                        const keys = Object.keys(modelData.record)
                        return keys.slice(0, 3).map(k => k + " " + panel.show(modelData.record[k])).join(" · ")
                    }
                    color: Theme.dim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            Text {
                x: 24
                visible: !link.up && link.modelData.count > link.modelData.rows.length
                text: "Ver los " + link.modelData.count + " →"
                color: Theme.tint
                font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent
                    onClicked: panel.follow(link.modelData.table, link.modelData.column, link.modelData.value)
                }
            }
        }
    }
}
