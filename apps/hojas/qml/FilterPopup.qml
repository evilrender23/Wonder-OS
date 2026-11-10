import QtQuick
import QtQuick.Controls.Basic

/// The values of a filtered column: tick the ones to keep, or sort by it.
Popup {
    id: popup
    required property var host
    readonly property var book: host.book
    property int column: 0
    property var values: []

    function show(col, x, y) {
        column = col
        values = book.filterValues(col)
        search.text = ""
        popup.x = Math.max(8, Math.min(x - 4, parent.width - width - 8))
        popup.y = Math.max(8, Math.min(y + 2, parent.height - height - 8))
        open()
    }
    function apply() {
        const kept = values.filter(v => v.on).map(v => v.text)
        book.setFilterAllowed(column, kept, kept.length === values.length)
        close()
    }
    function setAll(on) {
        values = values.map(v => ({text: v.text, on: on}))
    }

    parent: Overlay.overlay
    width: 236
    height: Math.min(380, 150 + Math.min(values.length, 9) * 26)
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 11; color: Theme.menu; border.color: Theme.border }

    contentItem: Item {
        Row {
            id: sorts
            spacing: 6
            DialogButton { implicitWidth: 107; text: "A → Z"; onClicked: { popup.close(); popup.host.curCol = popup.column; popup.host.run("sort-asc") } }
            DialogButton { implicitWidth: 107; text: "Z → A"; onClicked: { popup.close(); popup.host.curCol = popup.column; popup.host.run("sort-desc") } }
        }
        Field {
            id: search
            y: 36
            width: parent.width
            placeholderText: "Buscar valores"
        }
        Row {
            y: 70
            spacing: 12
            Text {
                text: "Todos"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea { anchors.fill: parent; onClicked: popup.setAll(true) }
            }
            Text {
                text: "Ninguno"
                color: Theme.accent
                font.pixelSize: 12
                MouseArea { anchors.fill: parent; onClicked: popup.setAll(false) }
            }
        }
        ListView {
            id: list
            y: 92
            width: parent.width
            height: parent.height - 92 - 36
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: popup.values
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                readonly property bool shown: search.text === "" || modelData.text.toLowerCase().includes(search.text.toLowerCase())
                width: list.width
                height: shown ? 26 : 0
                visible: shown
                Check {
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    on: entry.modelData.on
                }
                Text {
                    x: 28
                    width: parent.width - 32
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: entry.modelData.text === "" ? "(Vacías)" : entry.modelData.text
                    color: entry.modelData.text === "" ? Theme.dim : Theme.text
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const copy = popup.values.slice()
                        copy[entry.index] = {text: entry.modelData.text, on: !entry.modelData.on}
                        popup.values = copy
                    }
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 6
            DialogButton { implicitWidth: 86; text: "Cancelar"; onClicked: popup.close() }
            DialogButton { implicitWidth: 86; text: "Aplicar"; primary: true; active: popup.values.some(v => v.on); onClicked: popup.apply() }
        }
    }
}
