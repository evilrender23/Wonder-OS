import QtQuick
import QtQuick.Controls.Basic
import WonderlandBases

/// A report: the rows of a table or a query set on pages, grouped and
/// totalled; the page in sight is the one the PDF gets.
Item {
    id: page

    property var host
    property string key
    property string name
    property int objectId
    /// {title, subtitle, source, sql, columns: [{name, label, show, total}], groupBy, landscape}
    property var definition: ({title: "", columns: []})
    property string saved: "{}"
    readonly property bool inspectorOpen: host ? host.inspectorOpen : true
    readonly property bool dirty: JSON.stringify(definition) !== saved
    readonly property string status: view.error !== "" ? view.error
        : "Página " + (view.page + 1) + " de " + Math.max(1, view.pageCount) + " · " + host.count(view.rows) + " filas" + (view.truncated ? " (recortado)" : "")
    readonly property string detail: definition.source ? "Informe de " + definition.source : "Informe"
    readonly property var totals: [{id: "", label: "—"}, {id: "sum", label: "Suma"}, {id: "avg", label: "Media"}, {id: "count", label: "Cuenta"}, {id: "min", label: "Mínimo"}, {id: "max", label: "Máximo"}]
    readonly property var exportFilters: ["PDF (*.pdf)"]

    Component.onCompleted: {
        if (objectId > 0) {
            definition = App.db.object(objectId).definition ?? definition
            saved = JSON.stringify(definition)
        }
    }
    function apply(extra) {
        if (extra.source && objectId === 0 && !definition.source && !definition.sql)
            definition = App.defaultReport(extra.source)
    }
    function put(what, value) {
        const next = JSON.parse(JSON.stringify(definition))
        next[what] = value
        definition = next
    }
    function putColumn(index, what, value) {
        const next = JSON.parse(JSON.stringify(definition))
        next.columns[index][what] = value
        definition = next
    }
    function exportTo(path) {
        const error = view.exportPdf(path)
        host.say(error !== "" ? error : "Informe exportado a " + path)
    }
    function save(given) {
        const store = title => {
            const id = App.db.saveObject("report", title, definition, objectId)
            if (id === 0) {
                host.say(App.db.lastError || "No se pudo guardar el informe.")
                return
            }
            saved = JSON.stringify(definition)
            host.objectSaved(key, "report", id, title)
        }
        if (given)
            store(given)
        else if (objectId > 0)
            store(name)
        else
            host.askName("Guardar el informe", "Aparecerá en la barra lateral, bajo Informes.", definition.title ?? "", "Guardar", store)
    }
    function act(verb, arg) {
        switch (verb) {
        case "pdf":
        case "export":
            exportTo(arg)
            return true
        case "save":
            save(arg)
            return true
        case "page":
            view.page = Math.max(0, Math.min(view.pageCount - 1, Number(arg)))
            return true
        case "group":
            put("groupBy", arg)
            return true
        case "landscape":
            put("landscape", !definition.landscape)
            return true
        case "title":
            put("title", arg)
            return true
        case "total": {
            const at = arg.indexOf("=")
            const index = definition.columns.findIndex(c => c.name === arg.slice(0, at))
            if (index >= 0)
                putColumn(index, "total", arg.slice(at + 1))
            return index >= 0
        }
        case "refresh":
            view.refresh()
            return true
        }
        return false
    }

    Rectangle {
        id: bar
        width: parent.width
        height: 42
        z: 4
        color: Theme.window
        Row {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            TButton {
                symbol: "chevron-left"
                active: view.page > 0
                onClicked: view.page = view.page - 1
            }
            Text {
                width: 110
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: "Página " + (view.page + 1) + " de " + Math.max(1, view.pageCount)
                color: Theme.dim
                font.pixelSize: 13
            }
            TButton {
                symbol: "chevron-right"
                active: view.page < view.pageCount - 1
                onClicked: view.page = view.page + 1
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            TextButton {
                symbol: "save"
                text: page.objectId > 0 ? "Guardar" : "Guardar…"
                strong: page.dirty
                active: page.dirty || page.objectId === 0
                onClicked: page.save("")
            }
            TextButton {
                symbol: "file-text"
                text: "Exportar PDF…"
                active: view.error === ""
                onClicked: page.host.askExport(page.name || page.definition.title || "informe", path => page.exportTo(path), page.exportFilters)
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    Rectangle {
        id: desk
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: side.visible ? side.left : parent.right
        color: Theme.dark ? "#161617" : "#e9e9ec"
        clip: true
        // The sheet at the width of the pane; what does not fit is scrolled to.
        Flickable {
            id: scroll
            anchors.fill: parent
            contentWidth: width
            contentHeight: sheet.height + 48
            boundsBehavior: Flickable.StopAtBounds
            visible: view.error === ""
            ScrollBar.vertical: ScrollBar {}
            Rectangle {
                x: sheet.x - 1
                y: sheet.y + 2
                width: sheet.width + 2
                height: sheet.height + 3
                radius: 2
                color: "#1a000000"
            }
            Rectangle {
                id: sheet
                x: Math.round((scroll.width - width) / 2)
                y: 24
                width: Math.round(Math.min(scroll.width - 48, page.definition.landscape ? 980 : 720))
                height: Math.round(width * view.ratio)
                color: "#ffffff"
                border.color: "#22000000"
                ReportView {
                    id: view
                    anchors.fill: parent
                    database: App.db
                    definition: page.definition
                    onPageChanged: scroll.contentY = 0
                }
            }
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 80
            visible: view.error !== ""
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: view.error
            color: Theme.dim
            font.pixelSize: 14
        }
    }

    Inspector {
        id: side
        anchors.top: bar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: page.inspectorOpen
        heading: "Diseño del informe"
        Caption { text: "Título" }
        Field {
            width: parent.width
            text: page.definition.title ?? ""
            onEditingFinished: {
                if (text !== (page.definition.title ?? ""))
                    page.put("title", text)
            }
        }
        Caption { text: "Subtítulo" }
        Field {
            width: parent.width
            text: page.definition.subtitle ?? ""
            onEditingFinished: {
                if (text !== (page.definition.subtitle ?? ""))
                    page.put("subtitle", text)
            }
        }
        Caption { text: "Agrupar por" }
        FCombo {
            id: groupBox
            readonly property var names: [""].concat((page.definition.columns ?? []).map(c => c.name))
            width: parent.width
            model: names.map(n => n === "" ? "Sin grupos" : n)
            currentIndex: Math.max(0, names.indexOf(page.definition.groupBy ?? ""))
            onActivated: index => page.put("groupBy", names[index])
        }
        Check {
            width: parent.width
            text: "Hoja apaisada"
            checked: !!page.definition.landscape
            onToggled: on => page.put("landscape", on)
        }
        Item { width: 1; height: 4 }
        Caption { text: "Columnas y totales" }
        Repeater {
            model: page.definition.columns ?? []
            Item {
                id: line
                required property var modelData
                required property int index
                width: parent.width
                height: 30
                Check {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 104
                    text: line.modelData.label || line.modelData.name
                    checked: line.modelData.show !== false
                    onToggled: on => page.putColumn(line.index, "show", on)
                }
                FCombo {
                    anchors.right: parent.right
                    width: 98
                    model: page.totals.map(t => t.label)
                    currentIndex: Math.max(0, page.totals.findIndex(t => t.id === (line.modelData.total ?? "")))
                    onActivated: index => page.putColumn(line.index, "total", page.totals[index].id)
                }
            }
        }
    }
}
