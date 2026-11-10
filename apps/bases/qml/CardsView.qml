import QtQuick
import QtQuick.Controls.Basic

/// The rows of a table as a gallery of cards: the name on top, a few fields below.
Item {
    id: gallery

    property var model
    property int current: -1
    /// The column that names a record.
    property string titleColumn
    readonly property var shown: {
        if (!model)
            return []
        return model.fields.filter(f => f.name !== titleColumn && f.kind !== "blob").slice(0, 5)
    }
    signal picked(int row)
    signal opened(int row)

    function reveal() {
        if (current >= 0)
            view.positionViewAtIndex(current, GridView.Contain)
    }

    GridView {
        id: view
        anchors.fill: parent
        anchors.margins: 14
        anchors.rightMargin: 4
        readonly property int columns: Math.max(1, Math.floor(width / 250))
        cellWidth: Math.floor((width - 10) / columns)
        cellHeight: 52 + gallery.shown.length * 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 400
        model: gallery.model ? gallery.model.count : 0
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: cell
            required property int index
            readonly property var record: {
                App.db.dataRevision
                return gallery.model.record(index)
            }
            readonly property bool on: index === gallery.current
            width: view.cellWidth
            height: view.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.rightMargin: 10
                anchors.bottomMargin: 10
                radius: 10
                color: cell.on ? Theme.selection : (area.containsMouse ? Theme.hover : Theme.rest)
                border.color: cell.on ? Theme.tint : Theme.line
                Column {
                    x: 12
                    y: 10
                    width: parent.width - 24
                    spacing: 4
                    Text {
                        width: parent.width
                        text: {
                            const value = cell.record[gallery.titleColumn]
                            return value === null || value === undefined || value === "" ? "Registro " + (cell.index + 1) : gallery.model.display(value)
                        }
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        bottomPadding: 3
                    }
                    Repeater {
                        model: gallery.shown
                        Row {
                            id: line
                            required property var modelData
                            readonly property var value: cell.record[modelData.name]
                            readonly property bool empty: value === null || value === undefined
                            width: parent.width
                            height: 16
                            spacing: 6
                            Text {
                                width: Math.min(92, parent.width * 0.4)
                                text: line.modelData.name
                                color: Theme.faint
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width - Math.min(92, parent.width * 0.4) - 6
                                text: line.empty ? "—" : (line.modelData.kind === "bool" ? (Number(line.value) ? "Sí" : "No") : gallery.model.display(line.value))
                                color: line.empty ? Theme.faint : Theme.dim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: gallery.picked(cell.index)
                    onDoubleClicked: gallery.opened(cell.index)
                }
            }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: view.count === 0
        text: gallery.model && gallery.model.filtered ? "Ningún registro cumple los filtros" : "Sin registros"
        color: Theme.faint
        font.pixelSize: 14
    }
}
