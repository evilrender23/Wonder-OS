import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

/// The sheet: its cells, and over them the selection, the editor, the ranges
/// of the formula being typed, the charts and the floating pill.
FocusScope {
    id: view

    required property var host
    readonly property var book: host.book
    property real scrollX: 0
    property real scrollY: 0
    /// Read by whatever places itself over the cells: it moves when they do.
    readonly property int tick: book.revision + layoutTick
    property int layoutTick: 0
    readonly property real hw: grid.headerWidth
    readonly property real hh: grid.headerHeight
    readonly property real frozenW: { tick; return book.open ? book.colPos(book.freezeCols) : 0 }
    readonly property real frozenH: { tick; return book.open ? book.rowPos(book.freezeRows) : 0 }
    readonly property real maxX: { tick; return book.open ? Math.max(0, book.colPos(book.cols) - (width - hw) + 40) : 0 }
    readonly property real maxY: { tick; return book.open ? Math.max(0, book.rowPos(book.rows) - (height - hh) + 40) : 0 }

    clip: true

    Connections {
        target: view.book
        function onLayoutChanged() {
            view.layoutTick++
            view.scrollTo(view.scrollX, view.scrollY)
        }
    }

    function takeFocus() {
        keys.forceActiveFocus()
    }
    function focusEditor() {
        editor.forceActiveFocus()
    }
    function scrollTo(x, y) {
        scrollX = Math.max(0, Math.min(x, maxX))
        scrollY = Math.max(0, Math.min(y, maxY))
        if (book.open && (scrollY > maxY - 200 || scrollX > maxX - 200))
            book.reach(book.rowAt(scrollY + height) + 1, book.colAt(scrollX + width) + 1)
    }
    /// Scrolls just enough for a cell to be in sight.
    function reveal(row, col) {
        if (!book.open || width <= 0)
            return
        let x = scrollX
        let y = scrollY
        if (col >= book.freezeCols) {
            const left = book.colPos(col)
            const right = left + book.colWidth(col)
            if (left - x < frozenW)
                x = left - frozenW
            else if (right - x > width - hw - 14)
                x = Math.min(left - frozenW, right - (width - hw - 14))
        }
        if (row >= book.freezeRows) {
            const top = book.rowPos(row)
            const bottom = top + book.rowHeight(row)
            if (top - y < frozenH)
                y = top - frozenH
            else if (bottom - y > height - hh - 14)
                y = Math.min(top - frozenH, bottom - (height - hh - 14))
        }
        scrollTo(x, y)
    }

    // --- Where things are ---------------------------------------------------------

    function edgeX(col) {
        const p = book.colPos(col)
        return hw + (col >= book.freezeCols ? Math.max(p - scrollX, frozenW) : p)
    }
    function edgeY(row) {
        const p = book.rowPos(row)
        return hh + (row >= book.freezeRows ? Math.max(p - scrollY, frozenH) : p)
    }
    function box(r1, c1, r2, c2) {
        const x = edgeX(c1)
        const y = edgeY(r1)
        return Qt.rect(x, y, Math.max(0, edgeX(c2 + 1) - x), Math.max(0, edgeY(r2 + 1) - y))
    }
    function colAtX(x) {
        const inside = x - hw
        return book.colAt(inside < frozenW ? inside : inside + scrollX)
    }
    function rowAtY(y) {
        const inside = y - hh
        return book.rowAt(inside < frozenH ? inside : inside + scrollY)
    }

    function openFilterAt(row, col) {
        const b = box(row, col, row, col)
        const p = view.mapToItem(null, b.x, b.y + b.height)
        host.openFilter(col, p.x, p.y)
    }
    function openContextMenu() {
        const b = box(host.curRow, host.curCol, host.curRow, host.curCol)
        cellMenu.popup(view, b.x + 20, b.y + b.height)
    }

    GridItem {
        id: grid
        anchors.fill: parent
        scrollX: view.scrollX
        scrollY: view.scrollY
        revision: view.tick
        dark: Theme.dark
        paper: Theme.window
        ink: Theme.text
        dim: Theme.dim
        line: Theme.line
        tint: Theme.tint
        headerColor: Theme.chrome
        headers: true
        selR1: view.host.selection.r1
        selC1: view.host.selection.c1
        selR2: view.host.selection.r2
        selC2: view.host.selection.c2
    }

    // --- The mouse -----------------------------------------------------------------

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        /// cells, cols, rows, col-size, row-size, or nothing.
        property string drag: ""
        property int sized: -1
        property real sizedFrom: 0

        /// The column whose right edge is under x, in the headers; -1 when none.
        function colEdge(x, y) {
            if (y > view.hh || x < view.hw)
                return -1
            const col = view.colAtX(x - 4)
            return Math.abs(view.edgeX(col + 1) - x) <= 4 ? col : -1
        }
        function rowEdge(x, y) {
            if (x > view.hw || y < view.hh)
                return -1
            const row = view.rowAtY(y - 4)
            return Math.abs(view.edgeY(row + 1) - y) <= 4 ? row : -1
        }
        cursorShape: drag === "col-size" || (drag === "" && colEdge(mouseX, mouseY) >= 0) ? Qt.SplitHCursor
            : (drag === "row-size" || (drag === "" && rowEdge(mouseX, mouseY) >= 0) ? Qt.SplitVCursor : Qt.ArrowCursor)

        onPressed: event => {
            const host = view.host
            if (!view.book.open)
                return
            const row = view.rowAtY(event.y)
            const col = view.colAtX(event.x)
            const inCols = event.y < view.hh
            const inRows = event.x < view.hw
            // A formula that waits for a reference takes the cell that is clicked.
            if (host.editing && !inCols && !inRows && host.wantsReference()) {
                host.pointStart = host.pointStart >= 0 ? host.pointStart : host.editCursor
                pointRow = row
                pointCol = col
                host.insertReference(view.book.name(row, col, row, col))
                drag = "point"
                return
            }
            if (host.editing && !host.commit(0, 0))
                return
            view.takeFocus()
            if (inCols && inRows) {
                host.run("select-all")
                return
            }
            if (event.button === Qt.RightButton) {
                const s = host.selection
                const inside = row >= s.r1 && row <= s.r2 && col >= s.c1 && col <= s.c2
                if (inCols) {
                    if (col < s.c1 || col > s.c2)
                        host.selectArea({r1: 0, c1: col, r2: view.book.rows - 1, c2: col})
                    colMenu.popup()
                } else if (inRows) {
                    if (row < s.r1 || row > s.r2)
                        host.selectArea({r1: row, c1: 0, r2: row, c2: view.book.cols - 1})
                    rowMenu.popup()
                } else {
                    if (!inside)
                        host.select(row, col, false)
                    cellMenu.popup()
                }
                return
            }
            if (inCols) {
                const edge = colEdge(event.x, event.y)
                if (edge >= 0) {
                    drag = "col-size"
                    sized = edge
                    sizedFrom = view.edgeX(edge)
                    return
                }
                drag = "cols"
                if (event.modifiers & Qt.ShiftModifier) {
                    host.curCol = col
                } else {
                    host.selectArea({r1: 0, c1: col, r2: view.book.rows - 1, c2: col})
                }
                return
            }
            if (inRows) {
                const edge = rowEdge(event.x, event.y)
                if (edge >= 0) {
                    drag = "row-size"
                    sized = edge
                    sizedFrom = view.edgeY(edge)
                    return
                }
                drag = "rows"
                if (event.modifiers & Qt.ShiftModifier) {
                    host.curRow = row
                } else {
                    host.selectArea({r1: row, c1: 0, r2: row, c2: view.book.cols - 1})
                }
                return
            }
            const widget = (event.modifiers & Qt.ShiftModifier) ? "" : view.book.widgetAt(row, col)
            if (widget === "filter") {
                const b = view.box(row, col, row, col)
                if (event.x > b.x + b.width - 22) {
                    host.select(row, col, false)
                    view.openFilterAt(row, col)
                    return
                }
            } else if (widget === "check") {
                const b = view.box(row, col, row, col)
                if (Math.abs(event.y - (b.y + b.height / 2)) < 11) {
                    host.select(row, col, false)
                    view.book.toggleCheck(row, col)
                    return
                }
            }
            drag = "cells"
            host.select(row, col, (event.modifiers & Qt.ShiftModifier) !== 0)
        }
        property int pointRow: 0
        property int pointCol: 0
        onPositionChanged: event => {
            if (!pressed || drag === "")
                return
            const host = view.host
            if (drag === "col-size") {
                view.book.setColWidth(sized, sized, Math.max(12, event.x - sizedFrom))
                return
            }
            if (drag === "row-size") {
                view.book.setRowHeight(sized, sized, Math.max(8, event.y - sizedFrom))
                return
            }
            const row = view.rowAtY(Math.max(view.hh + 1, Math.min(event.y, view.height - 1)))
            const col = view.colAtX(Math.max(view.hw + 1, Math.min(event.x, view.width - 1)))
            if (drag === "point") {
                host.insertReference(view.book.name(pointRow, pointCol, row, col))
                return
            }
            if (drag === "cells" && (row !== host.curRow || col !== host.curCol)) {
                host.curRow = row
                host.curCol = col
                view.reveal(row, col)
            } else if (drag === "cols" && col !== host.curCol) {
                host.curCol = col
            } else if (drag === "rows" && row !== host.curRow) {
                host.curRow = row
            }
        }
        onReleased: {
            if (drag === "point")
                view.host.pointStart = -1
            drag = ""
        }
        onDoubleClicked: event => {
            if (!view.book.open || event.button !== Qt.LeftButton)
                return
            if (event.y < view.hh) {
                const edge = colEdge(event.x, event.y)
                if (edge >= 0)
                    view.book.fitCols(edge, edge)
                return
            }
            if (event.x < view.hw)
                return
            if (view.book.widgetAt(view.host.curRow, view.host.curCol) === "check")
                return
            view.host.beginEdit(undefined, "cell", false)
        }
        onWheel: event => {
            if (event.modifiers & Qt.ControlModifier) {
                view.host.run(event.angleDelta.y > 0 ? "zoom-in" : "zoom-out")
                return
            }
            const dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x / 120 * 96
            const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 72
            if (event.modifiers & Qt.ShiftModifier)
                view.scrollTo(view.scrollX - dy, view.scrollY)
            else
                view.scrollTo(view.scrollX - dx, view.scrollY - dy)
        }
    }

    // --- Over the cells ----------------------------------------------------------------

    Item {
        id: over
        x: view.hw
        y: view.hh
        width: view.width - view.hw
        height: view.height - view.hh
        clip: true
        visible: view.book.open

        // The ranges of the formula being typed, each in its colour.
        Repeater {
            model: view.host.references
            Rectangle {
                required property var modelData
                readonly property rect at: { view.tick; view.scrollX; view.scrollY; return view.box(modelData.r1, modelData.c1, modelData.r2, modelData.c2) }
                visible: modelData.here
                x: at.x - view.hw - 1
                y: at.y - view.hh - 1
                width: at.width + 1
                height: at.height + 1
                color: Qt.alpha(modelData.color, 0.13)
                border.color: modelData.color
                border.width: 2
                radius: 2
            }
        }

        // What was copied waits here.
        Shape {
            id: ants
            readonly property var area: { view.tick; return view.book.open ? view.book.copied() : ({}) }
            readonly property rect at: { view.tick; view.scrollX; view.scrollY; return area.r1 === undefined ? Qt.rect(0, 0, 0, 0) : view.box(area.r1, area.c1, area.r2, area.c2) }
            visible: area.r1 !== undefined
            x: at.x - view.hw - 1
            y: at.y - view.hh - 1
            ShapePath {
                strokeColor: Theme.tint
                strokeWidth: 1.5
                fillColor: "transparent"
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                startX: 0.5
                startY: 0.5
                PathLine { x: ants.at.width + 0.5; y: 0.5 }
                PathLine { x: ants.at.width + 0.5; y: ants.at.height + 0.5 }
                PathLine { x: 0.5; y: ants.at.height + 0.5 }
                PathLine { x: 0.5; y: 0.5 }
            }
        }

        // The selection.
        Rectangle {
            id: selectionBox
            readonly property var s: view.host.selection
            readonly property rect at: { view.tick; view.scrollX; view.scrollY; return view.box(s.r1, s.c1, s.r2, s.c2) }
            visible: view.host.chartId === 0
            x: at.x - view.hw - 1
            y: at.y - view.hh - 1
            width: at.width + 1
            height: at.height + 1
            color: view.host.single ? "transparent" : Qt.alpha(Theme.tint, Theme.dark ? 0.16 : 0.10)
            border.color: Theme.tint
            border.width: 2
            radius: 2
        }
        // The cell the keyboard is on stays clear inside a selection.
        Rectangle {
            readonly property var e: { view.tick; return view.book.open ? view.book.extent(view.host.curRow, view.host.curCol) : ({r1: 0, c1: 0, r2: 0, c2: 0}) }
            readonly property rect at: { view.tick; view.scrollX; view.scrollY; return view.box(e.r1, e.c1, e.r2, e.c2) }
            visible: !view.host.single && view.host.chartId === 0
            x: at.x - view.hw
            y: at.y - view.hh
            width: Math.max(0, at.width - 1)
            height: Math.max(0, at.height - 1)
            color: "transparent"
            border.color: Qt.alpha(Theme.tint, 0.55)
            border.width: 1
        }
        // The handle that fills: drag it down or right.
        Rectangle {
            id: handle
            visible: selectionBox.visible && !view.host.editing && selectionBox.width > 8 && selectionBox.height > 8
            x: selectionBox.x + selectionBox.width - 5
            y: selectionBox.y + selectionBox.height - 5
            width: 8
            height: 8
            radius: 2
            color: Theme.tint
            border.color: Theme.window
            border.width: 1
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.CrossCursor
                property var from: null
                onPressed: from = view.host.selection
                onPositionChanged: event => {
                    if (from === null)
                        return
                    const p = mapToItem(view, event.x, event.y)
                    const row = view.rowAtY(Math.max(view.hh + 1, p.y))
                    const col = view.colAtX(Math.max(view.hw + 1, p.x))
                    // Down or right, whichever went further.
                    if (row - from.r2 >= col - from.c2)
                        fillPreview.target = {r1: from.r1, c1: from.c1, r2: Math.max(from.r2, row), c2: from.c2}
                    else
                        fillPreview.target = {r1: from.r1, c1: from.c1, r2: from.r2, c2: Math.max(from.c2, col)}
                }
                onReleased: {
                    const t = fillPreview.target
                    if (from !== null && t !== null && (t.r2 > from.r2 || t.c2 > from.c2)) {
                        view.book.fillSeries(from.r1, from.c1, from.r2, from.c2, t.r1, t.c1, t.r2, t.c2)
                        view.host.selectArea(t)
                    }
                    from = null
                    fillPreview.target = null
                }
            }
        }
        Rectangle {
            id: fillPreview
            property var target: null
            readonly property rect at: { view.tick; return target === null ? Qt.rect(0, 0, 0, 0) : view.box(target.r1, target.c1, target.r2, target.c2) }
            visible: target !== null
            x: at.x - view.hw - 1
            y: at.y - view.hh - 1
            width: at.width + 1
            height: at.height + 1
            color: "transparent"
            border.color: Qt.alpha(Theme.tint, 0.6)
            border.width: 1
        }

        // The charts sit on the sheet and scroll with it.
        Repeater {
            model: { view.tick; return view.book.open ? view.book.charts() : [] }
            Rectangle {
                id: card
                required property var modelData
                readonly property bool on: view.host.chartId === modelData.id
                x: modelData.x - view.scrollX + (move.drag.active ? 0 : 0)
                y: modelData.y - view.scrollY
                width: modelData.width
                height: modelData.height
                radius: 8
                color: Theme.dark ? "#262628" : "#ffffff"
                border.color: on ? Theme.tint : Theme.border
                border.width: on ? 2 : 1
                ChartItem {
                    anchors.fill: parent
                    anchors.margins: 6
                    chartId: card.modelData.id
                    revision: view.tick
                    dark: Theme.dark
                }
                MouseArea {
                    id: move
                    anchors.fill: parent
                    property point from
                    property bool moved: false
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                    onPressed: event => {
                        if (view.host.editing && !view.host.commit(0, 0))
                            return
                        view.host.chartId = card.modelData.id
                        view.takeFocus()
                        from = Qt.point(event.x, event.y)
                        moved = false
                    }
                    onPositionChanged: event => {
                        if (!pressed)
                            return
                        if (Math.abs(event.x - from.x) + Math.abs(event.y - from.y) > 4)
                            moved = true
                        if (moved) {
                            card.x += event.x - from.x
                            card.y += event.y - from.y
                        }
                    }
                    onReleased: {
                        if (moved)
                            view.book.moveChart(card.modelData.id, Math.max(0, card.x + view.scrollX), Math.max(0, card.y + view.scrollY))
                    }
                }
                Rectangle {
                    visible: card.on
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: -4
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.tint
                    border.color: Theme.window
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        cursorShape: Qt.SizeFDiagCursor
                        property point from
                        onPressed: event => from = mapToItem(view, event.x, event.y)
                        onPositionChanged: event => {
                            const p = mapToItem(view, event.x, event.y)
                            card.width = Math.max(200, card.width + p.x - from.x)
                            card.height = Math.max(140, card.height + p.y - from.y)
                            from = p
                        }
                        onReleased: {
                            view.book.beginGroup()
                            view.book.setChart(card.modelData.id, "width", card.width / view.book.zoom)
                            view.book.setChart(card.modelData.id, "height", card.height / view.book.zoom)
                            view.book.endGroup()
                        }
                    }
                }
            }
        }

        // The cell being typed in.
        Rectangle {
            id: editBox
            readonly property var e: { view.tick; return view.book.open ? view.book.extent(view.host.curRow, view.host.curCol) : ({r1: 0, c1: 0, r2: 0, c2: 0}) }
            readonly property rect at: { view.tick; view.scrollX; view.scrollY; return view.box(e.r1, e.c1, e.r2, e.c2) }
            visible: view.host.editing
            x: at.x - view.hw - 1
            y: at.y - view.hh - 1
            width: Math.min(over.width - x - 4, Math.max(at.width + 1, editor.contentWidth + 18))
            height: Math.max(at.height + 1, 24)
            color: Theme.window
            border.color: Theme.tint
            border.width: 2
            radius: 2
            // The same text with its references in colour, under a see-through editor.
            Text {
                id: painted
                visible: view.host.inFormula
                anchors.fill: editor
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                font: editor.font
                color: Theme.text
                text: Colors.formula(view.host.editText, view.host.references, Theme.text)
            }
            TextInput {
                id: editor
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 7
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: view.host.inFormula ? "transparent" : Theme.text
                font.pixelSize: Math.round(13 * view.book.zoom)
                selectionColor: Qt.alpha(Theme.accent, 0.35)
                selectedTextColor: Theme.text
                selectByMouse: true
                cursorDelegate: Rectangle {
                    width: 1.5
                    color: Theme.text
                    visible: editor.activeFocus && blink.on
                    Timer {
                        id: blink
                        property bool on: true
                        interval: 530
                        repeat: true
                        running: editor.activeFocus
                        onTriggered: on = !on
                    }
                }
                onTextEdited: {
                    view.host.pointStart = -1
                    view.host.editText = text
                }
                onCursorPositionChanged: if (activeFocus) view.host.editCursor = cursorPosition
                Connections {
                    target: view.host
                    function onEditTextChanged() {
                        if (editor.text !== view.host.editText)
                            editor.text = view.host.editText
                    }
                    function onCursorWanted(position) {
                        if (view.host.editPlace === "cell") {
                            editor.text = view.host.editText
                            editor.cursorPosition = position
                        }
                    }
                }
                Keys.onPressed: event => view.editKey(event, true)
            }
        }
    }

    /// The keys that mean the same in the cell and in the formula bar.
    function editKey(event, inCell) {
        const host = view.host
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (event.modifiers & Qt.AltModifier)
                return
            if (event.modifiers & Qt.ControlModifier)
                host.commit(1, 0, true)
            else
                host.commit(shift ? -1 : 1, 0)
            event.accepted = true
            return
        case Qt.Key_Tab:
            if (!host.acceptCompletion(0))
                host.commit(0, 1)
            event.accepted = true
            return
        case Qt.Key_Backtab:
            host.commit(0, -1)
            event.accepted = true
            return
        case Qt.Key_Escape:
            host.cancelEdit()
            event.accepted = true
            return
        case Qt.Key_F4:
            return
        }
        // Typed straight over a cell, the arrows leave it; a formula keeps them.
        if (inCell && host.quickEdit && !host.inFormula && event.modifiers === Qt.NoModifier) {
            const step = {[Qt.Key_Up]: [-1, 0], [Qt.Key_Down]: [1, 0], [Qt.Key_Left]: [0, -1], [Qt.Key_Right]: [0, 1]}[event.key]
            if (step !== undefined) {
                host.commit(step[0], step[1])
                event.accepted = true
            }
        }
    }

    // --- The keyboard, when no cell is open -----------------------------------------------

    Item {
        id: keys
        focus: true
        Keys.onPressed: event => {
            const host = view.host
            if (!view.book.open)
                return
            const shift = (event.modifiers & Qt.ShiftModifier) !== 0
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            const page = Math.max(1, Math.floor((view.height - view.hh - view.frozenH) / (21 * view.book.zoom)) - 1)
            event.accepted = true
            switch (event.key) {
            case Qt.Key_Up: host.move(-1, 0, shift, ctrl); return
            case Qt.Key_Down: host.move(1, 0, shift, ctrl); return
            case Qt.Key_Left: host.move(0, -1, shift, ctrl); return
            case Qt.Key_Right: host.move(0, 1, shift, ctrl); return
            case Qt.Key_Tab: host.move(0, 1, false, false); return
            case Qt.Key_Backtab: host.move(0, -1, false, false); return
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (host.chartId !== 0)
                    return
                host.move(shift ? -1 : 1, 0, false, false)
                return
            case Qt.Key_PageDown: host.select(host.curRow + page, host.curCol, shift); return
            case Qt.Key_PageUp: host.select(Math.max(0, host.curRow - page), host.curCol, shift); return
            case Qt.Key_Home:
                host.select(ctrl ? 0 : host.curRow, 0, shift)
                return
            case Qt.Key_End: {
                const used = view.book.usedArea()
                host.select(ctrl ? Math.max(0, used.r2) : host.curRow, Math.max(0, used.c2), shift)
                return
            }
            case Qt.Key_F2:
                host.beginEdit(undefined, "cell", false)
                return
            case Qt.Key_Delete:
            case Qt.Key_Backspace:
                if (host.chartId !== 0)
                    host.run("chart-remove")
                else
                    host.run("clear")
                return
            case Qt.Key_Escape:
                if (host.chartId !== 0)
                    host.chartId = 0
                else if (view.book.copied().r1 !== undefined)
                    view.book.forgetCopy()
                else if (host.concentrate)
                    host.concentrate = false
                else
                    event.accepted = false
                return
            case Qt.Key_Space:
                if (!ctrl && !shift && view.book.widgetAt(host.curRow, host.curCol) === "check") {
                    view.book.toggleCheck(host.curRow, host.curCol)
                    return
                }
                if (ctrl) {
                    host.selectArea({r1: 0, c1: host.selection.c1, r2: view.book.rows - 1, c2: host.selection.c2})
                    return
                }
                if (shift) {
                    host.selectArea({r1: host.selection.r1, c1: 0, r2: host.selection.r2, c2: view.book.cols - 1})
                    return
                }
                break
            }
            // Anything that types starts the cell over.
            if (event.text !== "" && !ctrl && !(event.modifiers & Qt.AltModifier) && event.text.charCodeAt(0) >= 32 && event.text.charCodeAt(0) !== 127 && host.chartId === 0) {
                host.beginEdit(event.text, "cell", true)
                return
            }
            event.accepted = false
        }
    }

    // --- Scroll bars -------------------------------------------------------------------------

    ScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors.right: parent.right
        y: view.hh + view.frozenH
        height: view.height - y - 10
        visible: view.book.open && view.maxY > 0
        size: Math.max(0.04, height / (height + view.maxY))
        position: view.maxY > 0 ? view.scrollY / view.maxY * (1 - size) : 0
        policy: ScrollBar.AsNeeded
        onPositionChanged: if (pressed) view.scrollTo(view.scrollX, position / (1 - size) * view.maxY)
        contentItem: Rectangle {
            implicitWidth: 7
            radius: 3.5
            color: Theme.text
            opacity: vbar.pressed ? 0.5 : (vbar.hovered ? 0.4 : 0.22)
        }
    }
    ScrollBar {
        id: hbar
        orientation: Qt.Horizontal
        anchors.bottom: parent.bottom
        x: view.hw + view.frozenW
        width: view.width - x - 10
        visible: view.book.open && view.maxX > 0
        size: Math.max(0.04, width / (width + view.maxX))
        position: view.maxX > 0 ? view.scrollX / view.maxX * (1 - size) : 0
        policy: ScrollBar.AsNeeded
        onPositionChanged: if (pressed) view.scrollTo(position / (1 - size) * view.maxX, view.scrollY)
        contentItem: Rectangle {
            implicitHeight: 7
            radius: 3.5
            color: Theme.text
            opacity: hbar.pressed ? 0.5 : (hbar.hovered ? 0.4 : 0.22)
        }
    }

    // --- The floating pill: what the selection most likely wants -----------------------------

    QuickPill {
        id: pill
        host: view.host
        room: view.width - view.hw - 20
        readonly property rect at: selectionBox.at
        visible: view.book.open && !view.host.modalOpen && !view.host.editing && view.host.chartId === 0 && !view.host.concentrate && mouse.drag === ""
            && (!view.host.single || view.host.info.kind !== undefined && view.host.info.kind !== "empty")
            && at.width > 0 && at.height > 0
        // Under the selection, and under what the sheet holds there when that
        // is in sight, not to cover it; over the selection when there is no room.
        x: Math.max(view.hw + 6, Math.min(at.x, view.width - width - 14))
        y: {
            view.tick
            let under = at.y + at.height + 8
            const used = view.book.open ? view.book.usedArea() : {r2: 0}
            if (view.host.selection.r2 < used.r2)
                under = Math.max(under, view.edgeY(used.r2 + 1) + 8)
            if (under + height < view.height - 12)
                return Math.max(view.hh + 6, under)
            return at.y - height - 8 > view.hh ? at.y - height - 8 : view.height - height - 14
        }
    }

    // --- Menus ---------------------------------------------------------------------------------

    FMenu {
        id: cellMenu
        FMenuItem { text: "Cortar"; onTriggered: view.host.run("cut") }
        FMenuItem { text: "Copiar"; onTriggered: view.host.run("copy") }
        FMenuItem { text: "Pegar"; onTriggered: view.host.run("paste") }
        FMenuItem { text: "Pegar solo los valores"; onTriggered: view.host.run("paste-values") }
        FSeparator {}
        FMenuItem { text: "Insertar filas encima"; onTriggered: view.host.run("insert-row") }
        FMenuItem { text: "Insertar columnas a la izquierda"; onTriggered: view.host.run("insert-col") }
        FMenuItem { text: "Eliminar las filas"; onTriggered: view.host.run("delete-row") }
        FMenuItem { text: "Eliminar las columnas"; onTriggered: view.host.run("delete-col") }
        FSeparator {}
        FMenuItem { text: "Borrar el contenido"; onTriggered: view.host.run("clear") }
        FMenuItem { text: "Quitar el formato"; onTriggered: view.host.run("clear-format") }
        FSeparator {}
        FMenuItem { text: view.host.info.merged ? "Separar las celdas" : "Combinar las celdas"; enabled: view.host.info.merged === true || !view.host.single; onTriggered: view.host.run("merge") }
        FMenuItem { text: "Ordenar A → Z"; onTriggered: view.host.run("sort-asc") }
        FMenuItem { text: "Ordenar Z → A"; onTriggered: view.host.run("sort-desc") }
        FMenuItem { text: view.host.table.index !== undefined ? "Dejar de ser tabla" : "Convertir en tabla"; onTriggered: view.host.run("table") }
    }
    FMenu {
        id: colMenu
        FMenuItem { text: "Insertar columnas a la izquierda"; onTriggered: view.host.run("insert-col") }
        FMenuItem { text: "Eliminar las columnas"; onTriggered: view.host.run("delete-col") }
        FMenuItem { text: "Ajustar el ancho al contenido"; onTriggered: view.host.run("fit-col") }
        FMenuItem { text: "Ocultar"; onTriggered: view.host.run("hide-col") }
        FMenuItem { text: "Mostrar las ocultas"; onTriggered: view.host.run("show-all") }
        FSeparator {}
        FMenuItem { text: "Tipo: texto"; onTriggered: view.host.run("type-text") }
        FMenuItem { text: "Tipo: número"; onTriggered: view.host.run("type-number") }
        FMenuItem { text: "Tipo: fecha"; onTriggered: view.host.run("type-date") }
        FMenuItem { text: "Tipo: casilla"; onTriggered: view.host.run("type-check") }
        FSeparator {}
        FMenuItem { text: "Ordenar A → Z"; onTriggered: view.host.run("sort-asc") }
        FMenuItem { text: "Ordenar Z → A"; onTriggered: view.host.run("sort-desc") }
    }
    FMenu {
        id: rowMenu
        FMenuItem { text: "Insertar filas encima"; onTriggered: view.host.run("insert-row") }
        FMenuItem { text: "Eliminar las filas"; onTriggered: view.host.run("delete-row") }
        FMenuItem { text: "Ocultar"; onTriggered: view.host.run("hide-row") }
        FMenuItem { text: "Mostrar las ocultas"; onTriggered: view.host.run("show-all") }
    }
}
