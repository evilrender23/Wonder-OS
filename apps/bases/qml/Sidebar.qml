import QtQuick
import QtQuick.Controls.Basic

/// What the database holds, by kind: tables, queries, forms and reports.
Rectangle {
    id: side

    property var host
    /// The key of the tab in sight.
    property string currentKey
    readonly property string filter: host ? host.sideFilter : ""
    /// What the menu of an entry is about.
    property var target: null

    readonly property var groups: {
        App.db.schemaRevision
        App.db.objectsRevision
        App.db.dataRevision
        if (!App.db.isOpen)
            return []
        const needle = fold(filter.trim())
        const keep = entry => needle === "" || fold(entry.name).includes(needle)
        const tables = App.db.tables().map(t => ({kind: "table", view: t.type === "view", name: t.name, id: 0, key: "table:" + t.name,
            note: t.type === "view" ? "" : host.count(App.db.rowCount(t.name))}))
        const kept = kind => App.db.objects(kind).map(o => ({kind: kind, view: false, name: o.name, id: o.id, key: kind + "#" + o.id, note: ""}))
        return [
            {title: "Tablas", command: "newtable", empty: "Ninguna tabla", entries: tables.filter(keep)},
            {title: "Consultas", command: "newquery", empty: "Ninguna consulta guardada", entries: kept("query").filter(keep)},
            {title: "Formularios", command: "newform", empty: "Ningún formulario", entries: kept("form").filter(keep)},
            {title: "Informes", command: "newreport", empty: "Ningún informe", entries: kept("report").filter(keep)},
        ]
    }

    function fold(text) {
        return String(text).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
    }
    /// Opens the menu of the entry of that name (tests).
    function menuFor(name) {
        for (const group of groups) {
            const entry = group.entries.find(e => e.name === name)
            if (entry) {
                target = entry
                menu.popup(side, 60, 120)
                return
            }
        }
    }

    color: Theme.sidebar
    clip: true

    Field {
        id: search
        x: 10
        y: 10
        width: parent.width - 20
        placeholderText: "Buscar"
        leftPadding: 28
        text: side.filter
        onTextEdited: side.host.sideFilter = text
        Sym {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            name: "search"
            size: 13
            color: Theme.faint
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: 46
        anchors.bottomMargin: 40
        contentHeight: list.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: list
            x: 8
            width: side.width - 16
            Repeater {
                model: side.groups
                Column {
                    id: group
                    required property var modelData
                    width: list.width
                    Item {
                        width: parent.width
                        height: 30
                        Caption {
                            x: 8
                            y: 12
                            width: parent.width - 40
                            text: group.modelData.title
                        }
                        TButton {
                            anchors.right: parent.right
                            y: 8
                            width: 22
                            height: 22
                            symbol: "plus"
                            size: 13
                            tone: Theme.dim
                            active: group.modelData.command !== "newtable" || !App.db.readOnly
                            onClicked: side.host.exec(group.modelData.command)
                        }
                    }
                    Text {
                        x: 8
                        width: parent.width - 16
                        height: 24
                        visible: group.modelData.entries.length === 0
                        verticalAlignment: Text.AlignVCenter
                        text: side.filter.trim() !== "" ? "Nada coincide" : group.modelData.empty
                        color: Theme.faint
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: group.modelData.entries
                        Rectangle {
                            id: row
                            required property var modelData
                            readonly property bool on: modelData.key === side.currentKey
                            width: group.width
                            height: 28
                            radius: 6
                            color: on ? Theme.sideSelection : (area.containsMouse ? Theme.hover : "transparent")
                            Sym {
                                x: 8
                                anchors.verticalCenter: parent.verticalCenter
                                name: Theme.objectSymbol(row.modelData.view ? "view" : row.modelData.kind)
                                size: 14
                                color: row.on ? Theme.tint : Theme.dim
                            }
                            Text {
                                x: 30
                                width: parent.width - 38 - note.width - 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.name
                                color: Theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                id: note
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.note
                                color: Theme.faint
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: area
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        side.target = row.modelData
                                        menu.popup(row, mouse.x, mouse.y)
                                    } else {
                                        side.host.openObject(row.modelData.kind, row.modelData.name, row.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                    Item { width: 1; height: 6 }
                }
            }
        }
    }

    // The diagram of the whole database, always at hand.
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        x: 8
        width: parent.width - 16
        height: 28
        radius: 6
        color: side.currentKey === "relations" ? Theme.sideSelection : (relationsArea.containsMouse ? Theme.hover : "transparent")
        Sym {
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            name: "workflow"
            size: 14
            color: side.currentKey === "relations" ? Theme.tint : Theme.dim
        }
        Text {
            x: 30
            anchors.verticalCenter: parent.verticalCenter
            text: "Relaciones"
            color: Theme.text
            font.pixelSize: 13
        }
        MouseArea {
            id: relationsArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: side.host.exec("relations")
        }
    }
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    FMenu {
        id: menu
        FMenuItem {
            text: "Abrir"
            onTriggered: side.host.openObject(side.target.kind, side.target.name, side.target.id)
        }
        FMenuItem {
            text: "Diseño"
            visible: !!side.target && side.target.kind === "table" && !side.target.view
            height: visible ? implicitHeight : 0
            onTriggered: side.host.openObject("table", side.target.name, 0, {view: "design"})
        }
        FMenuItem {
            text: "Formulario nuevo"
            visible: !!side.target && side.target.kind === "table" && !side.target.view
            height: visible ? implicitHeight : 0
            onTriggered: side.host.exec("newform", side.target.name)
        }
        FMenuItem {
            text: "Informe nuevo"
            visible: !!side.target && side.target.kind === "table"
            height: visible ? implicitHeight : 0
            onTriggered: side.host.exec("newreport", side.target.name)
        }
        FSeparator {}
        FMenuItem {
            text: "Renombrar…"
            enabled: !App.db.readOnly && !!side.target && !side.target.view
            onTriggered: side.host.renameObject(side.target.kind, side.target.name, side.target.id)
        }
        FMenuItem {
            text: "Eliminar…"
            enabled: !App.db.readOnly
            onTriggered: side.host.dropObject(side.target.kind, side.target.name, side.target.id)
        }
    }
}
