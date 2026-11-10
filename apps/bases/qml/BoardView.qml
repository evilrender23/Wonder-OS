import QtQuick
import QtQuick.Controls.Basic

/// The rows of a table as a board: a lane per value of one column, with its
/// count; moving a card to another lane writes that value.
Item {
    id: board

    property var model
    /// The column the lanes come from.
    property string column
    property string titleColumn
    /// The key of the card picked, as text.
    property string currentKey
    readonly property bool editable: !!model && model.editable
    readonly property var lanes: {
        App.db.dataRevision
        if (!model || column === "")
            return []
        model.count
        model.conditions
        return model.groups(column, 40)
    }
    readonly property var detail: {
        if (!model)
            return []
        return model.fields.filter(f => f.name !== titleColumn && f.name !== column && f.kind !== "blob" && !(f.pk > 0)).slice(0, 2)
    }
    signal picked(var key, var record)
    signal said(string message)

    /// Writes the lane's value on the card. `lane` is an index into lanes.
    function move(key, lane) {
        const target = lanes[lane]
        if (!target || !editable)
            return false
        if (!model.store(key, column, target.empty ? null : target.value)) {
            said(model.error)
            return false
        }
        return true
    }
    /// The card at `at` of lane `from` goes to lane `to` (tests).
    function moveCard(from, at, to) {
        const card = lanes[from] ? lanes[from].rows[at] : null
        return card ? move(card.key, to) : false
    }

    ListView {
        id: view
        anchors.fill: parent
        anchors.margins: 14
        anchors.bottomMargin: 6
        orientation: ListView.Horizontal
        spacing: 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: board.lanes
        ScrollBar.horizontal: ScrollBar {}
        delegate: Rectangle {
            id: lane
            required property var modelData
            required property int index
            width: 256
            height: view.height - 10
            radius: 10
            color: drop.containsDrag ? Theme.selection : Theme.rest
            border.color: drop.containsDrag ? Theme.tint : "transparent"
            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["bases/card"]
                onDropped: dropped => board.move(dropped.source.cardKey, lane.index)
            }
            Rectangle {
                x: 12
                y: 14
                width: 8
                height: 8
                radius: 4
                color: lane.modelData.empty ? Theme.faint : Qt.hsla((lane.index * 0.137 + 0.96) % 1, 0.55, Theme.dark ? 0.62 : 0.48, 1)
            }
            Text {
                x: 27
                y: 9
                width: parent.width - 39 - tally.width - 8
                text: lane.modelData.label
                color: lane.modelData.empty ? Theme.dim : Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                font.italic: lane.modelData.empty
                elide: Text.ElideRight
            }
            Rectangle {
                id: tally
                anchors.right: parent.right
                anchors.rightMargin: 10
                y: 8
                width: tallyText.implicitWidth + 14
                height: 20
                radius: 10
                color: Theme.field
                Text {
                    id: tallyText
                    anchors.centerIn: parent
                    text: lane.modelData.count
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            ListView {
                id: cards
                anchors.fill: parent
                anchors.topMargin: 36
                anchors.margins: 8
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: lane.modelData.rows
                ScrollBar.vertical: ScrollBar {}
                footer: Text {
                    width: cards.width
                    height: visible ? 26 : 0
                    visible: lane.modelData.count > lane.modelData.rows.length
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: "y " + (lane.modelData.count - lane.modelData.rows.length) + " más"
                    color: Theme.faint
                    font.pixelSize: 11
                }
                delegate: Item {
                    id: slot
                    required property var modelData
                    readonly property string keyText: JSON.stringify(modelData.key)
                    width: cards.width
                    height: 34 + board.detail.length * 17
                    Rectangle {
                        id: card
                        property var cardKey: slot.modelData.key
                        width: slot.width
                        height: slot.height
                        radius: 8
                        color: Theme.dark ? "#3a3a3d" : "#ffffff"
                        border.color: slot.keyText === board.currentKey ? Theme.tint : (Theme.dark ? "transparent" : "#16000000")
                        Drag.active: cardArea.drag.active
                        Drag.keys: ["bases/card"]
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: 14
                        states: State {
                            when: cardArea.drag.active
                            ParentChange { target: card; parent: board }
                            PropertyChanges { card.opacity: 0.85; card.z: 50 }
                        }
                        Column {
                            x: 10
                            y: 8
                            width: parent.width - 42
                            spacing: 2
                            Text {
                                width: parent.width
                                text: {
                                    const value = slot.modelData.record[board.titleColumn]
                                    return value === null || value === undefined || value === "" ? "(sin nombre)" : board.model.display(value)
                                }
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Repeater {
                                model: board.detail
                                Text {
                                    required property var modelData
                                    readonly property var value: slot.modelData.record[modelData.name]
                                    width: parent.width
                                    height: 15
                                    text: modelData.name + ": " + (value === null || value === undefined ? "—" : board.model.display(value))
                                    color: Theme.dim
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea {
                            id: cardArea
                            anchors.fill: parent
                            drag.target: board.editable ? card : null
                            onClicked: board.picked(slot.modelData.key, slot.modelData.record)
                            onReleased: {
                                if (drag.active)
                                    card.Drag.drop()
                                card.x = 0
                                card.y = 0
                            }
                        }
                        TButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            y: 4
                            width: 22
                            height: 22
                            visible: board.editable
                            symbol: "ellipsis"
                            size: 13
                            tone: Theme.faint
                            onClicked: {
                                moveMenu.key = slot.modelData.key
                                moveMenu.popup(this, 0, height)
                            }
                        }
                    }
                }
            }
        }
    }
    Column {
        anchors.centerIn: parent
        visible: board.lanes.length === 0
        spacing: 6
        Sym {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "kanban"
            size: 30
            color: Theme.faint
        }
        Text {
            text: board.column === "" ? "Elige arriba la columna que agrupa el tablero" : "Sin registros"
            color: Theme.faint
            font.pixelSize: 14
        }
    }

    FMenu {
        id: moveMenu
        property var key: []
        Repeater {
            model: board.lanes
            FMenuItem {
                required property var modelData
                required property int index
                text: "Mover a «" + modelData.label + "»"
                onTriggered: board.move(moveMenu.key, index)
            }
        }
    }
}
