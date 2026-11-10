import QtQuick
import QtQuick.Controls.Basic

/// The list view: one row per item, sortable by its columns.
FocusScope {
    id: view
    required property var win
    required property DirModel dir
    property int renamingRow: -1
    property int dropRow: -1
    readonly property bool searching: dir.search !== ""
    readonly property var columns: [
        { title: qsTr("Name"), key: DirModel.ByName, width: 0 },
        { title: searching ? qsTr("Where") : qsTr("Date Modified"), key: searching ? -1 : DirModel.ByModified, width: searching ? 260 : 170 },
        { title: qsTr("Size"), key: DirModel.BySize, width: 90 },
        { title: qsTr("Kind"), key: DirModel.ByKind, width: 150 },
    ]
    readonly property real nameWidth: Math.max(160, width - 260 - (searching ? 260 : 170))

    function columnWidth(i) {
        return i === 0 ? nameWidth : columns[i].width
    }
    function cleanUp() {}
    function cleanUpBy(key) {}
    function placeAtMenu(name) {}

    focus: true
    Keys.onPressed: event => {
        if (renamingRow >= 0)
            return
        const extend = event.modifiers & Qt.ShiftModifier
        const move = row => {
            if (row < 0 || row >= dir.count)
                return
            if (extend)
                dir.extendTo(row)
            else
                dir.selectOnly(row)
        }
        if (event.key === Qt.Key_Up && !(event.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
            move(dir.currentRow < 0 ? dir.count - 1 : dir.currentRow - 1)
        else if (event.key === Qt.Key_Down && !(event.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
            move(dir.currentRow + 1)
        else if (!win.handleKey(event))
            return
        event.accepted = true
    }
    Connections {
        target: view.dir
        function onCurrentRowChanged() {
            if (view.dir.currentRow >= 0)
                list.positionViewAtIndex(view.dir.currentRow, ListView.Contain)
        }
    }

    Rectangle {
        id: header
        width: parent.width
        height: 24
        color: Theme.window
        z: 2
        Row {
            anchors.fill: parent
            Repeater {
                model: view.columns
                Item {
                    id: head
                    required property var modelData
                    required property int index
                    readonly property bool sorted: view.win.listSortKey === modelData.key
                    width: view.columnWidth(index)
                    height: header.height
                    Text {
                        x: head.index === 0 ? 34 : 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: head.modelData.title
                        color: Theme.dim
                        font.pixelSize: 11
                        font.bold: head.sorted
                    }
                    Sym {
                        visible: head.sorted
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        size: 11
                        name: view.win.listAscending ? "chevron-up" : "chevron-down"
                        color: Theme.dim
                    }
                    Rectangle {
                        visible: head.index > 0
                        width: 1
                        height: 14
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.border
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: head.modelData.key >= 0
                        onClicked: {
                            if (head.sorted) {
                                view.win.listAscending = !view.win.listAscending
                            } else {
                                view.win.listSortKey = head.modelData.key
                                view.win.listAscending = head.modelData.key === DirModel.ByName || head.modelData.key === DirModel.ByKind
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: header.height
        clip: true
        model: view.dir
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton
        ScrollBar.vertical: ScrollBar {}

        delegate: Item {
            id: row
            required property int index
            required property string name
            required property string fileName
            required property url url
            required property string path
            required property string icon
            required property string kind
            required property string sizeText
            required property string modifiedText
            required property string where
            required property bool isDir
            required property bool isHidden
            required property bool selected
            required property var tags
            readonly property color ink: selected ? "white" : Theme.text
            readonly property color soft: selected ? "white" : Theme.dim
            width: list.width
            height: 24

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                radius: 5
                color: row.selected || view.dropRow === row.index ? Theme.accent : (row.index % 2 ? Theme.stripe : "transparent")
            }
            Image {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                sourceSize: Qt.size(32, 32)
                opacity: row.isHidden ? 0.5 : 1
                source: App.icons + row.icon
            }
            Text {
                id: nameText
                visible: view.renamingRow !== row.index
                x: 34
                width: Math.min(implicitWidth, view.nameWidth - 34 - 8 - dots.width)
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                elide: Text.ElideMiddle
                color: row.ink
                font.pixelSize: 13
            }
            Row {
                id: dots
                x: nameText.x + nameText.width + 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: -3
                Repeater {
                    model: row.tags
                    Rectangle {
                        required property string modelData
                        width: 9
                        height: 9
                        radius: 5
                        color: App.tagColor(modelData)
                        border.color: Theme.window
                    }
                }
            }
            Text {
                x: view.nameWidth + 8
                width: view.columnWidth(1) - 12
                anchors.verticalCenter: parent.verticalCenter
                text: view.searching ? row.where : row.modifiedText
                elide: Text.ElideMiddle
                color: row.soft
                font.pixelSize: 12
            }
            Text {
                x: view.nameWidth + view.columnWidth(1) + 8
                width: view.columnWidth(2) - 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.sizeText
                horizontalAlignment: Text.AlignRight
                color: row.soft
                font.pixelSize: 12
            }
            Text {
                x: view.nameWidth + view.columnWidth(1) + view.columnWidth(2) + 8
                width: view.columnWidth(3) - 14
                anchors.verticalCenter: parent.verticalCenter
                text: row.kind
                elide: Text.ElideRight
                color: row.soft
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property point press
                property bool armed: false
                property bool deferred: false
                onPressed: mouse => {
                    view.forceActiveFocus()
                    if (mouse.button === Qt.RightButton) {
                        if (!row.selected)
                            view.dir.selectOnly(row.index)
                        view.win.itemMenu()
                        return
                    }
                    press = Qt.point(mouse.x, mouse.y)
                    armed = true
                    deferred = row.selected && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                    if (!deferred)
                        view.dir.click(row.index, mouse.modifiers)
                }
                onPositionChanged: mouse => {
                    if (!armed || Math.abs(mouse.x - press.x) + Math.abs(mouse.y - press.y) < 7)
                        return
                    armed = false
                    deferred = false
                    if (!row.selected)
                        view.dir.selectOnly(row.index)
                    view.win.beginDrag(row, Qt.point(press.x, press.y), {})
                }
                onReleased: {
                    if (armed && deferred)
                        view.dir.selectOnly(row.index)
                    armed = false
                    deferred = false
                }
                onCanceled: armed = false
                onDoubleClicked: mouse => {
                    armed = false
                    if (mouse.button === Qt.LeftButton)
                        Qt.callLater(view.win.openRow, row.index)
                }
            }
            Loader {
                active: view.renamingRow === row.index
                x: 30
                width: view.nameWidth - 36
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: RenameField {
                    original: row.fileName
                    isDir: row.isDir
                    onDone: name => {
                        view.renamingRow = -1
                        view.win.commitRename(row.url, name)
                        view.forceActiveFocus()
                    }
                    onCancelled: {
                        view.renamingRow = -1
                        view.forceActiveFocus()
                    }
                }
            }
        }

        MouseArea {
            // Below the last row: empty ground.
            z: -1
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                view.forceActiveFocus()
                view.dir.clearSelection()
                if (mouse.button === Qt.RightButton)
                    view.win.backgroundMenu()
            }
        }
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            function target(drag) {
                const row = list.indexAt(drag.x + list.contentX, drag.y + list.contentY)
                if (row < 0 || !view.dir.get(row).isDir)
                    return -1
                return DragState.active && view.dir.isSelected(row) ? -1 : row
            }
            onPositionChanged: drag => view.dropRow = target(drag)
            onExited: view.dropRow = -1
            onDropped: drop => {
                const row = view.dropRow
                view.dropRow = -1
                const copy = App.ops.dropWantsCopy(DragState.active, drop.proposedAction)
                if (row >= 0)
                    App.ops.drop(drop.urls, view.dir.get(row).url, copy)
                else if (view.win.writable && (copy || !view.win.allHere(drop.urls)))
                    App.ops.drop(drop.urls, view.win.location, copy)
                else
                    return
                drop.acceptProposedAction()
            }
        }
    }
}
