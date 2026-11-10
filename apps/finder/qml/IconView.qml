import QtQuick
import QtQuick.Controls.Basic

/// Icons placed freely, as in Finder: each sits where the folder's layout
/// says, and dragging one changes that. A folder nobody has arranged flows in
/// rows; so does one arranged by name, kind, date or size.
FocusScope {
    id: view
    required property var win
    required property DirModel dir
    required property FolderLayout layout
    property int iconSize: 64
    property int renamingRow: -1

    readonly property real cellW: Math.max(iconSize + 40, 104)
    readonly property real cellH: iconSize + 54
    readonly property real pad: 14
    readonly property real flowWidth: Math.max(cellW, flick.width - 2 * pad)
    /// Free placement: the folder is a real one and isn't kept sorted.
    readonly property bool free: win.canArrange && (layout.arrange === "" || layout.arrange === "grid")
    /// File name → corner of its cell, without the padding.
    property var positions: ({})
    property real extentW: 0
    property real extentH: 0
    property bool animate: false
    property int dropRow: -1
    /// Where the background menu was opened: a folder made from it appears there.
    property point menuPoint: Qt.point(-1, -1)

    function relayout() {
        const names = dir.fileNames()
        const placed = free && layout.arranged ? layout.place(names, flowWidth, cellW, cellH) : layout.flow(names, flowWidth, cellW, cellH)
        let w = 0
        let h = 0
        for (const name of names) {
            const p = placed[name]
            if (p.x > w)
                w = p.x
            if (p.y > h)
                h = p.y
        }
        extentW = names.length ? w + cellW : 0
        extentH = names.length ? h + cellH : 0
        positions = placed
    }
    function schedule() {
        Qt.callLater(relayout)
    }
    function settle() {
        animate = true
        settleTimer.restart()
    }
    Timer {
        id: settleTimer
        interval: 320
        onTriggered: view.animate = false
    }

    onFlowWidthChanged: schedule()
    onCellWChanged: schedule()
    onFreeChanged: schedule()
    Component.onCompleted: relayout()
    Connections {
        target: view.dir
        function onCountChanged() { view.schedule() }
        function onSortChanged() { view.schedule() }
        function onModelReset() { view.schedule() }
        function onRowsMoved() { view.schedule() }
        function onRenamed(from, to) {
            view.layout.rename(from, to)
            view.schedule()
        }
        function onCurrentRowChanged() { view.ensureVisible(view.dir.currentRow) }
    }
    Connections {
        target: view.layout
        function onStoredChanged() { view.schedule() }
        function onUrlChanged() {
            flick.contentY = 0
            flick.contentX = 0
            // Later, not now: the model may still hold the folder we are leaving.
            view.schedule()
        }
    }

    function nameOf(row) {
        return dir.get(row).fileName
    }
    function rowAt(x, y) {
        const names = dir.fileNames()
        for (let i = names.length - 1; i >= 0; --i) {
            const p = positions[names[i]]
            if (p && x >= pad + p.x + 8 && x < pad + p.x + cellW - 8 && y >= pad + p.y && y < pad + p.y + cellH - 4)
                return i
        }
        return -1
    }
    function folderAt(x, y) {
        const row = rowAt(x, y)
        if (row < 0 || !dir.get(row).isDir)
            return -1
        // A folder can't be dropped into itself.
        if (DragState.active && String(DragState.source) === String(win.location) && dir.isSelected(row))
            return -1
        return row
    }
    function ensureVisible(row) {
        if (row < 0)
            return
        const p = positions[nameOf(row)]
        if (!p)
            return
        const top = pad + p.y
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top - pad)
        else if (top + cellH > flick.contentY + flick.height)
            flick.contentY = top + cellH - flick.height + 4
        const left = pad + p.x
        if (left < flick.contentX)
            flick.contentX = Math.max(0, left - pad)
        else if (left + cellW > flick.contentX + flick.width)
            flick.contentX = left + cellW - flick.width + 4
    }
    /// The nearest icon in a direction: free icons form no rows to count along.
    function step(dx, dy, extend) {
        const names = dir.fileNames()
        if (!names.length)
            return
        let from = dir.currentRow
        if (from < 0) {
            dir.selectOnly(0)
            return
        }
        const origin = positions[names[from]]
        let best = -1
        let bestScore = 0
        for (let i = 0; i < names.length; ++i) {
            const p = positions[names[i]]
            if (i === from || !p)
                continue
            const along = dx !== 0 ? (p.x - origin.x) * dx : (p.y - origin.y) * dy
            const across = dx !== 0 ? Math.abs(p.y - origin.y) : Math.abs(p.x - origin.x)
            if (along < 1 || across > along * 1.5 + (dx !== 0 ? cellH : cellW) * 0.45)
                continue
            const score = along + across * 2.5
            if (best < 0 || score < bestScore) {
                best = i
                bestScore = score
            }
        }
        if (best < 0)
            return
        if (extend)
            dir.extendTo(best)
        else
            dir.selectOnly(best)
    }

    /// Puts the named icons at `at` (the corner for the grabbed one), each at its offset from it.
    function moveGroup(names, offsets, at) {
        if (!free || !names.length)
            return
        if (!layout.arranged)
            layout.freeze(dir.fileNames(), flowWidth, cellW, cellH)
        const wanted = {}
        names.forEach((name, i) => {
            const offset = offsets[name] ?? Qt.point(i * cellW, 0)
            wanted[name] = Qt.point(at.x + offset.x, at.y + offset.y)
        })
        settle()
        layout.moveTo(wanted, layout.arrange === "grid", cellW, cellH)
    }
    function cleanUp() {
        if (!free)
            return
        settle()
        if (!layout.arranged)
            layout.freeze(dir.fileNames(), flowWidth, cellW, cellH)
        else
            layout.cleanUp(dir.fileNames(), cellW, cellH)
    }
    /// Lays every icon out again in rows, in the order of `key`, and keeps that as the folder's arrangement.
    function cleanUpBy(key) {
        if (!free)
            return
        settle()
        win.sortOverride = key
        layout.freeze(dir.fileNames(), flowWidth, cellW, cellH)
        win.sortOverride = -1
    }
    function beginDrag(cell, row, point) {
        if (!dir.isSelected(row))
            dir.selectOnly(row)
        const origin = positions[nameOf(row)]
        const offsets = {}
        for (const other of dir.selectedRows()) {
            const name = nameOf(other)
            const p = positions[name]
            if (p)
                offsets[name] = Qt.point(p.x - origin.x, p.y - origin.y)
        }
        win.beginDrag(cell, point, offsets)
    }
    function handleDrop(drop) {
        const target = dropRow
        dropRow = -1
        if (!drop.urls.length)
            return
        const copy = App.ops.dropWantsCopy(DragState.active, drop.proposedAction)
        if (target >= 0) {
            App.ops.drop(drop.urls, dir.get(target).url, copy)
            drop.acceptProposedAction()
            return
        }
        const hot = DragState.active ? DragState.hotSpot : Qt.point(cellW / 2, cellH / 2)
        const at = Qt.point(drop.x - pad - hot.x, drop.y - pad - hot.y)
        const names = win.namesOf(drop.urls)
        const offsets = DragState.active ? DragState.offsets : {}
        if (win.allHere(drop.urls) && !copy) {
            // Already in this folder: the drop only says where they go.
            moveGroup(names, offsets, at)
            drop.accept(Qt.IgnoreAction)
            return
        }
        if (!win.writable)
            return
        if (free) {
            if (!layout.arranged)
                layout.freeze(dir.fileNames(), flowWidth, cellW, cellH)
            layout.reserve(names, at, flowWidth, cellW, cellH)
        }
        App.ops.drop(drop.urls, win.location, copy)
        drop.acceptProposedAction()
    }
    /// A new item takes the place the menu was opened at.
    function placeAtMenu(name) {
        if (free && layout.arranged && menuPoint.x >= 0)
            layout.reserve([name], Qt.point(menuPoint.x - pad - cellW / 2, menuPoint.y - pad - cellH / 2), flowWidth, cellW, cellH)
        menuPoint = Qt.point(-1, -1)
    }

    focus: true
    Keys.onPressed: event => {
        if (renamingRow >= 0)
            return
        const extend = event.modifiers & Qt.ShiftModifier
        if (event.key === Qt.Key_Left)
            step(-1, 0, extend)
        else if (event.key === Qt.Key_Right)
            step(1, 0, extend)
        else if (event.key === Qt.Key_Up && !(event.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
            step(0, -1, extend)
        else if (event.key === Qt.Key_Down && !(event.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
            step(0, 1, extend)
        else if (!win.handleKey(event))
            return
        event.accepted = true
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(width, view.extentW + 2 * view.pad)
        contentHeight: Math.max(height, view.extentH + 2 * view.pad)
        boundsBehavior: Flickable.StopAtBounds
        // The mouse draws the selection band and drags icons; scrolling is the wheel's.
        acceptedButtons: Qt.NoButton
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        MouseArea {
            id: ground
            width: flick.contentWidth
            height: flick.contentHeight
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property point origin
            property var keep: []
            property bool banding: false
            onPressed: mouse => {
                view.forceActiveFocus()
                if (mouse.button === Qt.RightButton) {
                    view.dir.clearSelection()
                    view.menuPoint = Qt.point(mouse.x, mouse.y)
                    view.win.backgroundMenu()
                    return
                }
                origin = Qt.point(mouse.x, mouse.y)
                keep = mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier) ? view.dir.selectedRows() : []
                if (!keep.length)
                    view.dir.clearSelection()
                banding = true
                band.width = 0
                band.height = 0
            }
            onPositionChanged: mouse => {
                if (!banding)
                    return
                band.x = Math.min(origin.x, mouse.x)
                band.y = Math.min(origin.y, mouse.y)
                band.width = Math.abs(mouse.x - origin.x)
                band.height = Math.abs(mouse.y - origin.y)
                const names = view.dir.fileNames()
                const rows = []
                for (let i = 0; i < names.length; ++i) {
                    const p = view.positions[names[i]]
                    if (!p)
                        continue
                    const left = view.pad + p.x + (view.cellW - view.iconSize) / 2
                    const top = view.pad + p.y
                    if (left < band.x + band.width && left + view.iconSize > band.x && top < band.y + band.height && top + view.cellH - 8 > band.y)
                        rows.push(i)
                }
                view.dir.selectRows(rows, keep)
            }
            onReleased: banding = false
            onCanceled: banding = false
        }

        DropArea {
            width: flick.contentWidth
            height: flick.contentHeight
            keys: ["text/uri-list"]
            onPositionChanged: drag => view.dropRow = view.folderAt(drag.x, drag.y)
            onExited: view.dropRow = -1
            onDropped: drop => view.handleDrop(drop)
        }

        Repeater {
            model: view.dir
            delegate: Item {
                id: cell
                required property int index
                required property string name
                required property string fileName
                required property url url
                required property string path
                required property string icon
                required property bool isDir
                required property bool isLink
                required property bool isHidden
                required property bool selected
                required property bool thumb
                required property var tags
                required property var modified
                readonly property var spot: view.positions[fileName]
                readonly property bool renaming: view.renamingRow === index
                x: view.pad + (spot ? spot.x : 0)
                y: view.pad + (spot ? spot.y : 0)
                width: view.cellW
                height: view.cellH
                visible: spot !== undefined
                Behavior on x {
                    enabled: view.animate
                    NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                }
                Behavior on y {
                    enabled: view.animate
                    NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
                }
                // Only what is near the viewport is built.
                Loader {
                    anchors.fill: parent
                    active: cell.visible && cell.y + cell.height > flick.contentY - 300 && cell.y < flick.contentY + flick.height + 300
                    sourceComponent: Item {
                        Rectangle {
                            id: plate
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 2
                            width: view.iconSize + 12
                            height: view.iconSize + 12
                            radius: 8
                            color: view.dropRow === cell.index ? Theme.accent : (cell.selected ? Theme.plate : "transparent")
                        }
                        Image {
                            id: picture
                            anchors.centerIn: plate
                            width: view.iconSize
                            height: view.iconSize
                            sourceSize: Qt.size(view.iconSize * 2, view.iconSize * 2)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: cell.thumb
                            opacity: cell.isHidden ? 0.5 : 1
                            property bool failed: false
                            source: cell.thumb && !failed ? "image://thumb/" + cell.path + "?" + cell.modified.getTime() : App.icons + cell.icon
                            onStatusChanged: if (status === Image.Error) failed = true
                        }
                        Rectangle {
                            visible: cell.isLink
                            x: picture.x
                            y: picture.y + picture.height - height
                            width: Math.max(14, view.iconSize / 4)
                            height: width
                            radius: 4
                            color: Theme.window
                            border.color: Theme.border
                            Sym {
                                anchors.centerIn: parent
                                size: parent.width - 4
                                name: "external-link"
                            }
                        }
                        Rectangle {
                            visible: !cell.renaming
                            anchors.centerIn: label
                            width: Math.min(label.width, label.contentWidth + 10)
                            height: label.contentHeight + 2
                            radius: 4
                            color: cell.selected ? Theme.accent : "transparent"
                        }
                        Text {
                            id: label
                            visible: !cell.renaming
                            x: 3
                            y: plate.y + plate.height + 4
                            width: parent.width - 6
                            text: cell.name
                            color: cell.selected ? "white" : Theme.text
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Row {
                            anchors.right: plate.right
                            anchors.rightMargin: 2
                            y: plate.y + 2
                            spacing: -4
                            Repeater {
                                model: cell.tags
                                Rectangle {
                                    required property string modelData
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: App.tagColor(modelData)
                                    border.color: Theme.window
                                }
                            }
                        }
                        MouseArea {
                            x: Math.min(plate.x, label.x + (label.width - label.contentWidth) / 2 - 5)
                            y: plate.y
                            width: parent.width - 2 * x
                            height: label.y + label.contentHeight + 2 - y
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property point press
                            property bool armed: false
                            property bool deferred: false
                            onPressed: mouse => {
                                view.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) {
                                    if (!cell.selected)
                                        view.dir.selectOnly(cell.index)
                                    view.win.itemMenu()
                                    return
                                }
                                press = Qt.point(mouse.x, mouse.y)
                                armed = true
                                // Pressing one of several selected icons may start dragging them all: only a plain click narrows the selection.
                                deferred = cell.selected && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                                if (!deferred)
                                    view.dir.click(cell.index, mouse.modifiers)
                            }
                            onPositionChanged: mouse => {
                                if (!armed || Math.abs(mouse.x - press.x) + Math.abs(mouse.y - press.y) < 7)
                                    return
                                armed = false
                                deferred = false
                                const point = mapToItem(cell, press.x, press.y)
                                view.beginDrag(cell, cell.index, Qt.point(point.x, point.y))
                            }
                            onReleased: {
                                if (armed && deferred)
                                    view.dir.selectOnly(cell.index)
                                armed = false
                                deferred = false
                            }
                            onCanceled: {
                                armed = false
                                deferred = false
                            }
                            onDoubleClicked: mouse => {
                                armed = false
                                if (mouse.button === Qt.LeftButton)
                                    Qt.callLater(view.win.openRow, cell.index)
                            }
                        }
                        Loader {
                            active: cell.renaming
                            x: 2
                            y: label.y - 3
                            width: parent.width - 4
                            sourceComponent: RenameField {
                                original: cell.fileName
                                isDir: cell.isDir
                                horizontalAlignment: TextInput.AlignHCenter
                                onDone: name => {
                                    view.renamingRow = -1
                                    view.win.commitRename(cell.url, name)
                                    view.forceActiveFocus()
                                }
                                onCancelled: {
                                    view.renamingRow = -1
                                    view.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: band
            visible: ground.banding && (width > 2 || height > 2)
            color: Theme.band
            border.color: Theme.faint
            z: 5
        }
    }
}
