import QtQuick
import QtQuick.Controls.Basic

/// The sheets of the document, and what the one in sight holds.
Rectangle {
    id: side
    required property var host
    readonly property var book: host.book
    color: Theme.sidebar

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    component Heading: Item {
        property string text
        property string symbol: ""
        signal pressed()
        width: side.width
        height: 30
        Text {
            x: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            text: parent.text
            color: Theme.dim
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        TButton {
            visible: parent.symbol !== ""
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            width: 24
            height: 22
            size: 14
            symbol: parent.symbol
            tone: Theme.dim
            onClicked: parent.pressed()
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.rightMargin: 1
        contentHeight: column.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            width: side.width - 1
            topPadding: 4

            Heading {
                text: "Hojas"
                symbol: "plus"
                onPressed: side.host.run("sheet-add")
            }
            Repeater {
                model: side.book.sheets
                Item {
                    id: entry
                    required property int index
                    required property string modelData
                    readonly property bool on: side.book.sheet === index
                    width: column.width
                    height: 30
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: 6
                        color: entry.on ? Theme.sideSelection : (area.containsMouse ? Theme.hover : "transparent")
                    }
                    Sym {
                        x: 17
                        anchors.verticalCenter: parent.verticalCenter
                        name: "table"
                        size: 14
                        color: entry.on ? Theme.tint : Theme.dim
                    }
                    Text {
                        x: 39
                        width: parent.width - 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (side.host.editing && !side.host.commit(0, 0))
                                return
                            side.book.sheet = entry.index
                            if (mouse.button === Qt.RightButton)
                                side.host.sheetMenuAt()
                        }
                        onDoubleClicked: side.host.renameSheet(entry.index)
                    }
                }
            }

            // What there is on the sheet: its tables and charts, one click away.
            Heading {
                visible: things.count > 0
                text: "En esta hoja"
            }
            Repeater {
                id: things
                model: {
                    side.book.revision
                    if (!side.book.open)
                        return []
                    const out = []
                    for (const c of side.book.charts()) {
                        const kinds = {column: "Columnas", bar: "Barras", line: "Líneas", area: "Área", pie: "Circular", scatter: "Dispersión"}
                        out.push({symbol: "chart-" + c.type, title: c.title !== "" ? c.title : "Gráfico · " + (kinds[c.type] || c.type), detail: c.range, chart: c.id, range: ""})
                    }
                    return out
                }
                Item {
                    id: thing
                    required property var modelData
                    readonly property bool on: modelData.chart !== 0 && side.host.chartId === modelData.chart
                    width: column.width
                    height: 30
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: 6
                        color: thing.on ? Theme.sideSelection : (thingArea.containsMouse ? Theme.hover : "transparent")
                    }
                    Sym {
                        x: 17
                        anchors.verticalCenter: parent.verticalCenter
                        name: thing.modelData.symbol
                        size: 14
                        color: Theme.dim
                    }
                    Text {
                        id: thingTitle
                        x: 39
                        width: Math.min(implicitWidth, parent.width - 54 - thingDetail.implicitWidth - 6)
                        anchors.verticalCenter: parent.verticalCenter
                        text: thing.modelData.title
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    Text {
                        id: thingDetail
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: thing.modelData.detail
                        color: Theme.faint
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: thingArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: side.host.chartId = thing.modelData.chart
                    }
                }
            }
        }
    }
}
