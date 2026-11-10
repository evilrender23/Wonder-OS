import QtQuick
import QtQuick.Controls.Basic

/// The tables as boxes and their foreign keys as lines between the columns
/// they tie. Boxes can be moved; where they are left is kept in the file.
Item {
    id: page

    property var host
    property string key
    readonly property bool dirty: false
    /// table → {x, y}, where a person left it.
    property var placed: ({})
    property string picked
    /// The box on the move: {name, x, y}
    property var dragging: null
    function at(table) {
        return dragging && dragging.name === table ? dragging : spots[table]
    }
    onDraggingChanged: lines.requestPaint()
    readonly property int boxWidth: 200
    readonly property int headHeight: 30
    readonly property int lineHeight: 22
    readonly property var tables: {
        App.db.schemaRevision
        return App.db.tableNames().map(name => ({name: name, columns: App.db.columns(name)}))
    }
    readonly property var links: {
        App.db.schemaRevision
        return App.db.relations()
    }
    /// table → {x, y}: by hand, or in columns by how far down the keys it is.
    readonly property var spots: {
        const depth = {}
        const seen = {}
        const walk = name => {
            if (depth[name] !== undefined)
                return depth[name]
            if (seen[name])
                return 0
            seen[name] = true
            let d = 0
            for (const link of links) {
                if (link.table === name && link.refTable !== name)
                    d = Math.max(d, walk(link.refTable) + 1)
            }
            depth[name] = d
            return d
        }
        const heights = {}
        const out = {}
        for (const t of tables) {
            const d = walk(t.name)
            const y = heights[d] ?? 30
            out[t.name] = placed[t.name] ?? {x: 30 + d * (boxWidth + 90), y: y}
            heights[d] = y + headHeight + t.columns.length * lineHeight + 34
        }
        return out
    }
    readonly property string status: tables.length + (tables.length === 1 ? " tabla · " : " tablas · ") + links.length + (links.length === 1 ? " relación" : " relaciones")
    readonly property string detail: picked

    Component.onCompleted: {
        const saved = App.db.objectNamed("layout", "«relaciones»")
        if (saved.definition && saved.definition.placed)
            placed = saved.definition.placed
    }
    function place(table, x, y) {
        const next = Object.assign({}, placed)
        next[table] = {x: Math.max(8, Math.round(x)), y: Math.max(8, Math.round(y))}
        placed = next
        keep()
    }
    function keep() {
        if (App.db.readOnly)
            return
        const saved = App.db.objectNamed("layout", "«relaciones»")
        App.db.saveObject("layout", "«relaciones»", {placed: placed}, saved.id ?? 0)
    }
    function tidy() {
        placed = ({})
        keep()
    }
    function act(verb, arg) {
        switch (verb) {
        case "move": {
            const parts = arg.split(",")
            place(parts[0], Number(parts[1]), Number(parts[2]))
            return true
        }
        case "tidy":
            tidy()
            return true
        case "pick":
            picked = arg
            return true
        case "refresh":
            return true
        }
        return false
    }
    onSpotsChanged: lines.requestPaint()
    onPickedChanged: lines.requestPaint()

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
                name: "workflow"
                size: 15
                color: Theme.tint
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Relaciones"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Las claves foráneas, de la columna que apunta a la que es apuntada"
                visible: bar.width > 760
                color: Theme.dim
                font.pixelSize: 12
            }
        }
        TextButton {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            symbol: "layout-grid"
            text: "Ordenar"
            active: Object.keys(page.placed).length > 0
            onClicked: page.tidy()
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    Flickable {
        id: desk
        anchors.fill: parent
        anchors.topMargin: bar.height
        contentWidth: Math.max(width, canvas.childrenRect.width + 60)
        contentHeight: Math.max(height, canvas.childrenRect.height + 60)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        Rectangle {
            width: desk.contentWidth
            height: desk.contentHeight
            color: Theme.window
            MouseArea {
                anchors.fill: parent
                onClicked: page.picked = ""
            }
        }
        Canvas {
            id: lines
            width: desk.contentWidth
            height: desk.contentHeight
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: Theme
                function onDarkChanged() { lines.requestPaint() }
            }
            function rowY(table, column) {
                const t = page.tables.find(e => e.name === table)
                const at = t ? Math.max(0, t.columns.findIndex(c => c.name === column)) : 0
                return page.at(table).y + page.headHeight + at * page.lineHeight + page.lineHeight / 2 + 4
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                for (const link of page.links) {
                    const from = page.at(link.table)
                    const to = page.at(link.refTable)
                    if (!from || !to)
                        continue
                    const on = page.picked === link.table || page.picked === link.refTable
                    const y1 = rowY(link.table, link.column)
                    const y2 = rowY(link.refTable, link.refColumn)
                    // Out of the side that faces the other box.
                    const leftward = from.x >= to.x + page.boxWidth / 2
                    const x1 = leftward ? from.x : from.x + page.boxWidth
                    const x2 = leftward ? to.x + page.boxWidth : (to.x > from.x + page.boxWidth / 2 ? to.x : to.x + page.boxWidth)
                    const d1 = leftward ? -1 : 1
                    const d2 = x2 === to.x ? -1 : 1
                    const reach = Math.max(40, Math.abs(x2 - x1) / 2)
                    ctx.strokeStyle = on ? Theme.tint : (Theme.dark ? "#7a7a80" : "#a3a3aa")
                    ctx.fillStyle = ctx.strokeStyle
                    ctx.lineWidth = on ? 2 : 1.4
                    // A box in the way is gone around, below it.
                    let floor = -1
                    for (let pass = 0; pass < 3; ++pass) {
                        const low = Math.max(y1, y2, floor)
                        for (const t of page.tables) {
                            if (t.name === link.table || t.name === link.refTable)
                                continue
                            const at = page.at(t.name)
                            const bottom = at.y + page.headHeight + t.columns.length * page.lineHeight + 8
                            if (at.x < Math.max(x1, x2) && at.x + page.boxWidth > Math.min(x1, x2) && at.y < low + 12 && bottom > Math.min(y1, y2) - 12)
                                floor = Math.max(floor, bottom + 18)
                        }
                    }
                    ctx.beginPath()
                    ctx.moveTo(x1, y1)
                    if (floor < 0) {
                        ctx.bezierCurveTo(x1 + d1 * reach, y1, x2 + d2 * reach, y2, x2, y2)
                    } else {
                        const out = x1 + d1 * 26
                        const back = x2 + d2 * 26
                        ctx.arcTo(out, y1, out, floor, 10)
                        ctx.arcTo(out, floor, back, floor, 10)
                        ctx.arcTo(back, floor, back, y2, 10)
                        ctx.arcTo(back, y2, x2, y2, 10)
                        ctx.lineTo(x2, y2)
                    }
                    ctx.stroke()
                    // Many on this end, one on that.
                    ctx.beginPath()
                    ctx.arc(x1, y1, 3.5, 0, Math.PI * 2)
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(x2, y2)
                    ctx.lineTo(x2 + d2 * 8, y2 - 4.5)
                    ctx.lineTo(x2 + d2 * 8, y2 + 4.5)
                    ctx.closePath()
                    ctx.fill()
                }
            }
        }
        Item {
            id: canvas
            Repeater {
                model: page.tables
                Rectangle {
                    id: box
                    required property var modelData
                    readonly property var spot: page.spots[modelData.name] ?? {x: 0, y: 0}
                    readonly property bool on: page.picked === modelData.name
                    x: spot.x
                    y: spot.y
                    width: page.boxWidth
                    height: page.headHeight + modelData.columns.length * page.lineHeight + 8
                    radius: 9
                    color: Theme.dark ? "#2c2c2e" : "#ffffff"
                    border.color: on ? Theme.tint : (Theme.dark ? "#48484c" : "#d4d4d9")
                    border.width: on ? 2 : 1
                    Rectangle {
                        x: 1
                        y: 1
                        width: parent.width - 2
                        height: page.headHeight
                        radius: 8
                        color: Theme.dark ? "#3a3a3d" : "#f3f3f5"
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 8
                            color: parent.color
                        }
                        Sym {
                            x: 9
                            anchors.verticalCenter: parent.verticalCenter
                            name: "table"
                            size: 13
                            color: Theme.tint
                        }
                        Text {
                            x: 29
                            width: parent.width - 38
                            anchors.verticalCenter: parent.verticalCenter
                            text: box.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    Column {
                        y: page.headHeight + 4
                        width: parent.width
                        Repeater {
                            model: box.modelData.columns
                            Item {
                                id: line
                                required property var modelData
                                readonly property bool tied: page.links.some(l => (l.table === box.modelData.name && l.column === modelData.name)
                                    || (l.refTable === box.modelData.name && l.refColumn === modelData.name))
                                width: box.width
                                height: page.lineHeight
                                Sym {
                                    x: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: line.modelData.pk > 0 ? "key" : (line.modelData.fkTable ? "link" : Theme.kindSymbol(line.modelData.kind))
                                    size: 11
                                    color: line.modelData.pk > 0 || line.modelData.fkTable ? Theme.tint : Theme.faint
                                }
                                Text {
                                    x: 28
                                    width: parent.width - 28 - kind.width - 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: line.modelData.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.weight: line.tied ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: kind
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: String(line.modelData.type || "").toLowerCase()
                                    color: Theme.faint
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        drag.target: box
                        drag.minimumX: 8
                        drag.minimumY: 8
                        drag.threshold: 4
                        onPressed: page.picked = box.modelData.name
                        // While it is dragged the lines follow the box itself.
                        onPositionChanged: {
                            if (drag.active)
                                page.dragging = {name: box.modelData.name, x: box.x, y: box.y}
                        }
                        onReleased: {
                            if (!page.dragging)
                                return
                            page.dragging = null
                            page.place(box.modelData.name, box.x, box.y)
                            box.x = Qt.binding(() => box.spot.x)
                            box.y = Qt.binding(() => box.spot.y)
                        }
                        onDoubleClicked: page.host.openObject("table", box.modelData.name)
                    }
                }
            }
        }
        Text {
            x: (desk.width - width) / 2
            y: desk.height / 2 - 10
            visible: page.tables.length === 0
            text: "Aún no hay tablas"
            color: Theme.faint
            font.pixelSize: 14
        }
    }
}
