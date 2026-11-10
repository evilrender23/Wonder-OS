import QtQuick
import QtQuick.Controls.Basic

/// Every slide at once, to see the whole talk and put it in order.
Rectangle {
    id: sorter
    property var ui
    readonly property var deck: App.deck
    color: Theme.desk

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 20
        anchors.rightMargin: 8
        clip: true
        readonly property int perRow: Math.max(2, Math.floor(width / 230))
        cellWidth: Math.floor((width - 12) / perRow)
        cellHeight: Math.round((cellWidth - 24) * sorter.deck.height / sorter.deck.width) + 44
        model: sorter.deck.count
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { width: 8 }
        delegate: Item {
            id: cell
            required property int index
            readonly property bool on: index === sorter.deck.current
            readonly property var info: { sorter.deck.revision; return sorter.deck.slide(index) }
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                x: 9
                y: 5
                width: parent.width - 18
                height: parent.height - 34
                radius: 7
                color: "transparent"
                border.color: cell.on ? Theme.tint : "transparent"
                border.width: 2
                SlideView {
                    anchors.fill: parent
                    anchors.margins: 3
                    deck: sorter.deck
                    index: cell.index
                    theme: sorter.ui.tryTheme
                    opacity: cell.info.hidden === true ? 0.4 : 1
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - 24
                spacing: 5
                Text {
                    text: cell.index + 1
                    color: cell.on ? Theme.tint : Theme.dim
                    font.pixelSize: 12
                    font.weight: cell.on ? Font.DemiBold : Font.Normal
                }
                Sym { visible: cell.info.hidden === true; anchors.verticalCenter: parent.verticalCenter; name: "eye-off"; size: 12; color: Theme.dim }
                Sym { visible: (cell.info.transition || "") !== ""; anchors.verticalCenter: parent.verticalCenter; name: "sparkles"; size: 12; color: Theme.dim }
                Sym { visible: (cell.info.notes || "") !== ""; anchors.verticalCenter: parent.verticalCenter; name: "sticky-note"; size: 12; color: Theme.dim }
            }
            MouseArea {
                anchors.fill: parent
                property point from
                property bool moved: false
                onPressed: mouse => {
                    sorter.deck.current = cell.index
                    from = mapToItem(grid, mouse.x, mouse.y)
                    moved = false
                }
                onPositionChanged: mouse => {
                    const at = mapToItem(grid, mouse.x, mouse.y)
                    if (Math.abs(at.x - from.x) + Math.abs(at.y - from.y) > 10)
                        moved = true
                    if (moved) {
                        const col = Math.max(0, Math.min(grid.perRow, Math.round(at.x / grid.cellWidth)))
                        const row = Math.max(0, Math.floor((at.y + grid.contentY) / grid.cellHeight))
                        drop.to = Math.min(sorter.deck.count, row * grid.perRow + col)
                        drop.x = grid.x + col * grid.cellWidth - 1
                        drop.y = grid.y + row * grid.cellHeight - grid.contentY + 5
                        drop.visible = true
                    }
                }
                onReleased: {
                    if (moved && drop.visible) {
                        const to = drop.to > cell.index ? drop.to - 1 : drop.to
                        if (to !== cell.index)
                            sorter.deck.moveSlide(cell.index, to)
                    }
                    drop.visible = false
                }
                onDoubleClicked: sorter.ui.sorting = false
            }
        }
    }
    Rectangle {
        id: drop
        property int to: 0
        visible: false
        width: 3
        height: grid.cellHeight - 34
        radius: 1.5
        color: Theme.tint
    }
}
