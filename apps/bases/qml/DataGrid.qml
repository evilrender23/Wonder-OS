import QtQuick
import QtQuick.Controls.Basic

/// The rows of a table or of a query: a sheet that reads only what is in
/// sight, sorts and filters by column, and edits each cell as its kind asks.
FocusScope {
    id: grid

    property string table
    property string sql
    property var parameters: ({})
    property alias linkColumn: rows.linkColumn
    property alias linkValue: rows.linkValue
    property alias search: rows.search
    /// The row of fields under the headers.
    property bool filtering: false
    /// A grid inside a form: no foot.
    property bool compact: false
    readonly property alias model: rows
    readonly property bool editable: rows.editable
    readonly property int count: rows.count
    property int curRow: -1
    property int curCol: 0
    /// Where a run of whole rows began; -1 when only a cell is picked.
    property int anchorRow: -1
    property var widths: []
    readonly property int rowHeight: 26
    readonly property bool editing: editor.visible
    readonly property var field: rows.fields[curCol] ?? null
    signal said(string message)

    function fmt(n) {
        return Number(n).toLocaleString(Qt.locale("es_ES"), "f", 0)
    }
    function columnX(column) {
        let x = 0
        for (let i = 0; i < column; ++i)
            x += widths[i] ?? 120
        return x
    }
    function reveal() {
        if (curRow >= 0 && curRow < rows.count && curCol >= 0 && curCol < rows.fields.length)
            view.positionViewAtCell(Qt.point(curCol, curRow), TableView.Contain)
    }
    function select(row, column, extend) {
        commit()
        if (rows.count === 0) {
            curRow = -1
            return
        }
        const before = curRow
        curRow = Math.max(0, Math.min(rows.count - 1, row))
        curCol = Math.max(0, Math.min(rows.fields.length - 1, column))
        if (!extend)
            anchorRow = -1
        else if (anchorRow < 0)
            anchorRow = Math.max(0, before)
        reveal()
        view.forceActiveFocus()
    }
    function selectRow(row, extend) {
        const from = extend && curRow >= 0 ? (anchorRow >= 0 ? anchorRow : curRow) : row
        select(row, curCol, false)
        anchorRow = from
    }
    function inSelection(row) {
        return anchorRow >= 0 && row >= Math.min(anchorRow, curRow) && row <= Math.max(anchorRow, curRow)
    }
    function selectedRows() {
        if (curRow < 0)
            return []
        const list = []
        const from = anchorRow >= 0 ? Math.min(anchorRow, curRow) : curRow
        const to = anchorRow >= 0 ? Math.max(anchorRow, curRow) : curRow
        for (let i = from; i <= to; ++i)
            list.push(i)
        return list
    }

    // --- Editing -----------------------------------------------------------------

    function edit(typed) {
        if (!rows.editable || curRow < 0 || !field || field.kind === "blob" || field.generated)
            return
        if (field.kind === "bool" && typed === undefined) {
            toggle()
            return
        }
        editor.options = field.fkTable ? App.db.lookup(field.fkTable, field.fkColumn) : []
        editor.wrong = false
        editor.text = typed !== undefined ? typed : rows.text(curRow, curCol)
        editor.visible = true
        editor.forceActiveFocus()
        if (typed === undefined)
            editor.selectAll()
        if (editor.options.length > 0)
            choices.open()
    }
    function toggle() {
        if (!rows.editable || curRow < 0 || !field || field.kind !== "bool")
            return
        const now = Number(rows.value(curRow, curCol)) !== 0 && rows.value(curRow, curCol) !== null && rows.value(curRow, curCol) !== undefined
        if (!rows.setValue(curRow, curCol, now ? 0 : 1))
            said(rows.error)
    }
    /// Stores what is in the editor. False when the database would not have it.
    function commit() {
        if (!editor.visible)
            return true
        if (!rows.setValue(curRow, curCol, editor.text)) {
            editor.wrong = true
            said(rows.error)
            return false
        }
        cancel()
        return true
    }
    function cancel() {
        choices.close()
        editor.visible = false
        view.forceActiveFocus()
    }
    function setNull() {
        if (rows.editable && curRow >= 0 && !rows.setNull(curRow, curCol))
            said(rows.error)
    }
    function addRow() {
        if (!rows.editable)
            return
        commit()
        const at = rows.insert({})
        if (at === -2) {
            // Some column must be given a value: ask for them all.
            recordDialog.ask(rows.error)
            return
        }
        landed(at)
    }
    function landed(at) {
        if (at >= 0) {
            select(at, firstTyped(), false)
        } else if (rows.sortColumn < 0 && !rows.filtered) {
            select(rows.count - 1, firstTyped(), false)
        } else {
            said("Registro añadido. No está a la vista con este orden o filtro.")
        }
    }
    /// The first column a person fills in: not the key that counts by itself.
    function firstTyped() {
        for (let i = 0; i < rows.fields.length; ++i) {
            const f = rows.fields[i]
            if (!(f.pk > 0 && f.kind === "integer") && !f.generated)
                return i
        }
        return 0
    }
    function removeSelected() {
        const list = selectedRows()
        if (!rows.editable || list.length === 0)
            return
        cancel()
        const first = list[0]
        if (!rows.remove(list)) {
            said(rows.error)
            return
        }
        anchorRow = -1
        curRow = Math.min(first, rows.count - 1)
        said(list.length === 1 ? "Registro eliminado. ⌃Z lo recupera." : fmt(list.length) + " registros eliminados. ⌃Z los recupera.")
    }
    function filterByCell() {
        if (curRow < 0 || !field)
            return
        const cell = rows.value(curRow, curCol)
        filtering = true
        rows.setFilter(field.name, cell === null || cell === undefined ? "null" : "=" + rows.text(curRow, curCol))
    }
    function clearFilters() {
        rows.clearFilters()
        rows.search = ""
    }

    GridModel {
        id: rows
        database: App.db
        table: grid.table
        sql: grid.sql
        parameters: grid.parameters
        onFieldsChanged: {
            const next = []
            for (let i = 0; i < fields.length; ++i)
                next.push(widthHint(i))
            grid.widths = next
            grid.cancel()
            if (grid.curCol >= fields.length)
                grid.curCol = 0
            view.forceLayout()
        }
        onCountChanged: {
            if (grid.curRow >= count)
                grid.curRow = count - 1
            if (grid.curRow < 0 && count > 0)
                grid.curRow = 0
            if (grid.anchorRow >= count)
                grid.anchorRow = -1
        }
    }

    // --- Headers -------------------------------------------------------------------

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28 + (grid.filtering ? 30 : 0)
        color: Theme.chrome
        clip: true
        z: 3
        Row {
            x: gutter.width - view.contentX
            height: parent.height
            Repeater {
                model: rows.fields
                Item {
                    id: head
                    required property var modelData
                    required property int index
                    width: grid.widths[index] ?? 120
                    height: header.height
                    Rectangle {
                        width: parent.width
                        height: 28
                        color: headArea.containsMouse ? Theme.hover : "transparent"
                        Sym {
                            id: glyph
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            name: head.modelData.pk > 0 ? "key" : (head.modelData.fkTable ? "link" : Theme.kindSymbol(head.modelData.kind))
                            size: 12
                            color: head.modelData.pk > 0 ? Theme.tint : Theme.faint
                        }
                        Text {
                            anchors.left: glyph.right
                            anchors.leftMargin: 6
                            anchors.right: arrow.visible ? arrow.left : parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: head.modelData.name
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Sym {
                            id: arrow
                            anchors.right: parent.right
                            anchors.rightMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            visible: rows.sortColumn === head.index
                            name: rows.sortDescending ? "arrow-down" : "arrow-up"
                            size: 12
                            color: Theme.tint
                        }
                        MouseArea {
                            id: headArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                grid.commit()
                                rows.sortBy(head.index)
                            }
                        }
                    }
                    Field {
                        id: filter
                        x: 3
                        y: 30
                        width: parent.width - 7
                        height: 24
                        implicitHeight: 24
                        visible: grid.filtering
                        font.pixelSize: 12
                        placeholderText: "Filtrar"
                        text: rows.filters[head.modelData.name] ?? ""
                        onTextEdited: later.restart()
                        onAccepted: {
                            later.stop()
                            rows.setFilter(head.modelData.name, text)
                        }
                        Timer {
                            id: later
                            interval: 350
                            onTriggered: rows.setFilter(head.modelData.name, filter.text)
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: Theme.line
                    }
                    // The edge that makes the column wider or narrower.
                    MouseArea {
                        anchors.right: parent.right
                        width: 7
                        height: 28
                        cursorShape: Qt.SplitHCursor
                        property real from: 0
                        property real was: 0
                        onPressed: mouse => {
                            from = mapToItem(grid, mouse.x, 0).x
                            was = head.width
                        }
                        onPositionChanged: mouse => {
                            const next = grid.widths.slice()
                            next[head.index] = Math.max(44, Math.round(was + mapToItem(grid, mouse.x, 0).x - from))
                            grid.widths = next
                            view.forceLayout()
                        }
                    }
                }
            }
        }
        // The corner over the numbers of the rows.
        Rectangle {
            width: gutter.width
            height: parent.height
            color: Theme.chrome
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.line
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // --- The numbers of the rows ------------------------------------------------------

    Rectangle {
        id: gutter
        anchors.top: header.bottom
        anchors.bottom: foot.top
        width: Math.max(38, String(rows.count).length * 8 + 18)
        color: Theme.chrome
        clip: true
        z: 2
        readonly property int first: Math.max(0, Math.floor(view.contentY / grid.rowHeight))
        Repeater {
            model: Math.ceil(gutter.height / grid.rowHeight) + 1
            Rectangle {
                required property int index
                readonly property int row: gutter.first + index
                visible: row < rows.count
                y: row * grid.rowHeight - view.contentY
                width: gutter.width
                height: grid.rowHeight
                color: grid.inSelection(row) ? Theme.selection : (row === grid.curRow ? Theme.hover : "transparent")
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.row + 1
                    color: parent.row === grid.curRow ? Theme.text : Theme.faint
                    font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: mouse => grid.selectRow(parent.row, mouse.modifiers & Qt.ShiftModifier)
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.line
        }
    }

    // --- The cells ------------------------------------------------------------------

    TableView {
        id: view
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: foot.top
        clip: true
        focus: true
        model: rows
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        columnWidthProvider: column => grid.widths[column] ?? 120
        rowHeightProvider: () => grid.rowHeight
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        delegate: Rectangle {
            id: cell
            required property int row
            required property int column
            required property var display
            required property bool isNull
            required property string kind
            required property var value
            readonly property bool current: row === grid.curRow && column === grid.curCol
            readonly property bool numeric: kind === "integer" || kind === "real"
            implicitHeight: grid.rowHeight
            color: grid.inSelection(row) ? Theme.selection : (current ? Theme.selection : (row === grid.curRow ? Theme.rest : "transparent"))
            Sym {
                anchors.centerIn: parent
                visible: cell.kind === "bool" && !cell.isNull
                name: Number(cell.value) !== 0 ? "square-check" : "square"
                size: 15
                color: Number(cell.value) !== 0 ? Theme.tint : Theme.faint
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                visible: cell.kind !== "bool" || cell.isNull
                text: cell.isNull ? "NULL" : String(cell.display).replace(/\n/g, " ⏎ ")
                color: cell.isNull ? Theme.faint : Theme.text
                font.pixelSize: 13
                font.italic: cell.isNull
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: cell.isNull && cell.kind === "bool" ? Text.AlignHCenter : (cell.numeric ? Text.AlignRight : Text.AlignLeft)
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.line
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.line
            }
            Rectangle {
                anchors.fill: parent
                visible: cell.current && grid.activeFocus
                color: "transparent"
                border.color: Theme.tint
                border.width: 2
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        if (!grid.inSelection(cell.row))
                            grid.select(cell.row, cell.column, false)
                        else
                            grid.curCol = cell.column
                        cellMenu.popup()
                        return
                    }
                    grid.select(cell.row, cell.column, mouse.modifiers & Qt.ShiftModifier)
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton)
                        grid.edit()
                }
            }
        }

        // What is being typed, over its cell.
        Field {
            id: editor
            property var options: []
            parent: view.contentItem
            visible: false
            z: 10
            x: grid.columnX(grid.curCol)
            y: Math.max(0, grid.curRow) * grid.rowHeight
            width: (grid.widths[grid.curCol] ?? 120) - 1
            height: grid.rowHeight - 1
            implicitHeight: grid.rowHeight - 1
            placeholderText: grid.field ? ({date: "AAAA-MM-DD", datetime: "AAAA-MM-DD HH:MM", integer: "0", real: "0,00"})[grid.field.kind] ?? "" : ""
            horizontalAlignment: grid.field && (grid.field.kind === "integer" || grid.field.kind === "real") ? TextInput.AlignRight : TextInput.AlignLeft
            background: Rectangle {
                color: Theme.window
                border.color: editor.wrong ? Theme.tint : Theme.accent
                border.width: 2
            }
            onTextEdited: wrong = false
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    grid.cancel()
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (choices.visible && choiceList.currentIndex >= 0 && choiceList.count > 0 && choices.picking)
                        text = String(choiceList.model[choiceList.currentIndex].value)
                    if (grid.commit() && grid.curRow < rows.count - 1)
                        grid.select(grid.curRow + 1, grid.curCol, false)
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                    const step = event.key === Qt.Key_Tab ? 1 : -1
                    if (grid.commit())
                        grid.select(grid.curRow, grid.curCol + step, false)
                    event.accepted = true
                } else if (choices.visible && (event.key === Qt.Key_Down || event.key === Qt.Key_Up)) {
                    choices.picking = true
                    choiceList.currentIndex = Math.max(0, Math.min(choiceList.count - 1, choiceList.currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                    event.accepted = true
                }
            }
        }
        // The values a foreign key may take.
        Popup {
            id: choices
            property bool picking: false
            parent: editor
            y: editor.height + 2
            width: Math.max(editor.width, 240)
            height: Math.min(choiceList.contentHeight + 10, 220)
            padding: 5
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
            onOpened: picking = false
            background: Rectangle {
                radius: 9
                color: Theme.menu
                border.color: Theme.border
            }
            contentItem: ListView {
                id: choiceList
                clip: true
                model: {
                    const typed = editor.text.trim().toLowerCase()
                    if (typed === "" || choices.picking)
                        return editor.options
                    return editor.options.filter(o => String(o.value).toLowerCase().startsWith(typed) || String(o.label).toLowerCase().includes(typed))
                }
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: option
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 24
                    radius: 5
                    color: ListView.isCurrentItem && choices.picking || optionArea.containsMouse ? Theme.accent : "transparent"
                    readonly property bool lit: ListView.isCurrentItem && choices.picking || optionArea.containsMouse
                    Text {
                        x: 8
                        width: 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(option.modelData.value)
                        color: option.lit ? "#ffffff" : Theme.dim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        x: 68
                        width: parent.width - 76
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.label
                        color: option.lit ? "#ffffff" : Theme.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: optionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            editor.text = String(option.modelData.value)
                            grid.commit()
                        }
                    }
                }
            }
        }

        Keys.onPressed: event => {
            const ctrl = event.modifiers & Qt.ControlModifier
            const shift = event.modifiers & Qt.ShiftModifier
            const page = Math.max(1, Math.floor(view.height / grid.rowHeight) - 1)
            event.accepted = true
            switch (event.key) {
            case Qt.Key_Up:
                grid.select(ctrl ? 0 : grid.curRow - 1, grid.curCol, shift)
                break
            case Qt.Key_Down:
                grid.select(ctrl ? rows.count - 1 : grid.curRow + 1, grid.curCol, shift)
                break
            case Qt.Key_Left:
                grid.select(grid.curRow, grid.curCol - 1, false)
                break
            case Qt.Key_Right:
                grid.select(grid.curRow, grid.curCol + 1, false)
                break
            case Qt.Key_Tab:
                grid.select(grid.curRow, grid.curCol + 1, false)
                break
            case Qt.Key_Backtab:
                grid.select(grid.curRow, grid.curCol - 1, false)
                break
            case Qt.Key_PageUp:
                grid.select(grid.curRow - page, grid.curCol, shift)
                break
            case Qt.Key_PageDown:
                grid.select(grid.curRow + page, grid.curCol, shift)
                break
            case Qt.Key_Home:
                grid.select(ctrl ? 0 : grid.curRow, 0, false)
                break
            case Qt.Key_End:
                grid.select(ctrl ? rows.count - 1 : grid.curRow, rows.fields.length - 1, false)
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_F2:
                grid.edit()
                break
            case Qt.Key_Space:
                if (grid.field && grid.field.kind === "bool")
                    grid.toggle()
                else
                    grid.edit(" ")
                break
            case Qt.Key_Delete:
            case Qt.Key_Backspace:
                if (grid.anchorRow >= 0)
                    grid.removeSelected()
                else
                    grid.setNull()
                break
            default:
                if (ctrl && event.key === Qt.Key_C && grid.curRow >= 0) {
                    App.copy(rows.text(grid.curRow, grid.curCol))
                } else if (!ctrl && !(event.modifiers & Qt.AltModifier) && event.text.length === 1 && event.text >= " ") {
                    grid.edit(event.text)
                } else {
                    event.accepted = false
                }
            }
        }
    }

    Column {
        anchors.centerIn: view
        visible: rows.count === 0
        spacing: 6
        width: Math.min(view.width - 40, 420)
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: rows.error !== "" ? "No se puede mostrar" : (rows.filtered ? "Ningún registro cumple el filtro" : "Sin registros")
            color: Theme.dim
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: rows.error !== "" ? rows.error : (rows.filtered ? "" : (rows.editable ? "Pulsa + para añadir el primero." : ""))
            color: Theme.faint
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    // --- The foot ---------------------------------------------------------------------

    Rectangle {
        id: foot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: grid.compact ? 0 : 26
        visible: !grid.compact
        color: Theme.chrome
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Text {
            id: where
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (rows.count === 0)
                    return "0 registros"
                const many = grid.selectedRows().length
                let said = many > 1 ? grid.fmt(many) + " registros seleccionados de " + grid.fmt(rows.count)
                    : "Registro " + grid.fmt(grid.curRow + 1) + " de " + grid.fmt(rows.count)
                if (rows.filtered && rows.total >= 0)
                    said += " · filtrado de " + grid.fmt(rows.total)
                return said
            }
            color: Theme.dim
            font.pixelSize: 12
        }
        Text {
            anchors.left: where.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: rows.count > 0 && rows.error !== "" ? rows.error
                : (!rows.editable && grid.table !== "" ? (App.db.readOnly ? "Solo lectura" : "Vista: no se edita") : (grid.field ? grid.field.name + (grid.field.type ? " · " + grid.field.type : "") + (grid.field.fkTable ? " → " + grid.field.fkTable : "") : ""))
            color: rows.count > 0 && rows.error !== "" ? Theme.tint : Theme.faint
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }

    FMenu {
        id: cellMenu
        FMenuItem {
            text: "Copiar"
            onTriggered: App.copy(rows.text(grid.curRow, grid.curCol))
        }
        FMenuItem {
            text: "Editar"
            enabled: rows.editable
            onTriggered: grid.edit()
        }
        FMenuItem {
            text: "Dejar vacío (NULL)"
            enabled: rows.editable
            onTriggered: grid.setNull()
        }
        FSeparator {}
        FMenuItem {
            text: "Filtrar por este valor"
            onTriggered: grid.filterByCell()
        }
        FMenuItem {
            text: "Quitar los filtros"
            enabled: rows.filtered
            onTriggered: grid.clearFilters()
        }
        FSeparator {}
        FMenuItem {
            text: "Añadir registro"
            enabled: rows.editable
            onTriggered: grid.addRow()
        }
        FMenuItem {
            text: grid.selectedRows().length > 1 ? "Eliminar registros" : "Eliminar registro"
            enabled: rows.editable
            onTriggered: grid.removeSelected()
        }
    }

    RecordDialog {
        id: recordDialog
        fields: rows.fields
        table: grid.table
        onAccepted: values => {
            const at = rows.insert(values)
            if (at === -2) {
                recordDialog.problem = rows.error
                return
            }
            recordDialog.close()
            grid.landed(at)
        }
    }
}
