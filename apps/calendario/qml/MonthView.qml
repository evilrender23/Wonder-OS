import QtQuick

/// A month, a box a day.
Item {
    id: view
    /// The day in focus; its month is the one shown.
    property int day
    signal picked(int day)
    signal create(int day)
    signal edit(var event)
    signal opened(int day)
    signal shifted(int months)

    readonly property var shown: App.parts(day)
    readonly property int first: App.monthStart(shown.year, shown.month)
    readonly property var head: App.parts(first)
    readonly property int gridStart: first - head.weekday
    readonly property int rows: Math.ceil((head.weekday + head.days) / 7)
    readonly property real cellWidth: width / 7
    readonly property real cellHeight: (height - names.height) / rows

    Row {
        id: names
        height: 26
        Repeater {
            model: 7
            Text {
                required property int index
                width: view.cellWidth
                height: names.height
                rightPadding: 10
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: App.weekdayName(index).slice(0, 3)
                color: Theme.dim
                font.pixelSize: 12
            }
        }
    }
    Rectangle {
        y: names.height - 1
        width: parent.width
        height: 1
        color: Theme.line
    }

    Repeater {
        model: view.rows * 7
        Item {
            id: box
            required property int index
            readonly property int day: view.gridStart + index
            readonly property var info: App.parts(day)
            readonly property bool inside: info.month === view.shown.month
            readonly property bool today: day === App.today
            readonly property var events: {
                App.agenda.revision
                return App.agenda.day(day)
            }
            /// How many lines fit under the number; the last becomes «+n más» when there are more.
            readonly property int room: Math.max(0, Math.floor((height - 30) / 20))
            readonly property int shown: events.length <= room ? events.length : Math.max(0, room - 1)

            x: (index % 7) * view.cellWidth
            y: names.height + Math.floor(index / 7) * view.cellHeight
            width: view.cellWidth
            height: view.cellHeight

            Rectangle {
                anchors.fill: parent
                color: !box.inside || box.info.weekday > 4 ? Theme.rest : "transparent"
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.line
                visible: box.index % 7 < 6
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.line
            }
            MouseArea {
                anchors.fill: parent
                onClicked: view.picked(box.day)
                onDoubleClicked: view.create(box.day)
            }
            Rectangle {
                id: badge
                anchors.right: parent.right
                anchors.rightMargin: 5
                y: 4
                height: 22
                width: Math.max(22, number.implicitWidth + 10)
                radius: 11
                color: box.today ? Theme.tint : (box.day === view.day ? Theme.sideSelection : "transparent")
                Text {
                    id: number
                    anchors.centerIn: parent
                    text: box.info.day === 1 ? "1 " + App.monthName(box.info.month).slice(0, 3) : box.info.day
                    color: box.today ? "#ffffff" : (box.inside ? Theme.text : Theme.faint)
                    font.pixelSize: 13
                    font.weight: box.today ? Font.DemiBold : Font.Normal
                }
            }
            Column {
                x: 3
                y: 29
                width: parent.width - 7
                spacing: 2
                opacity: box.inside ? 1 : 0.55
                Repeater {
                    model: box.events.slice(0, box.shown)
                    Chip {
                        required property var modelData
                        width: parent.width
                        reach: 4
                        untitled: box.index % 7 > 0
                        event: modelData
                        onClicked: view.edit(modelData)
                    }
                }
                Text {
                    visible: box.events.length > box.shown && box.room > 0
                    x: 5
                    height: 18
                    verticalAlignment: Text.AlignVCenter
                    text: "+" + (box.events.length - box.shown) + " más"
                    color: more.containsMouse ? Theme.text : Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    MouseArea {
                        id: more
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: view.opened(box.day)
                    }
                }
            }
            // No room for a single line: a dot says the day is not empty.
            Rectangle {
                visible: box.room === 0 && box.events.length > 0
                x: 8
                y: 12
                width: 6
                height: 6
                radius: 3
                color: box.events.length > 0 ? box.events[0].color : "transparent"
            }
        }
    }

    WheelHandler {
        property real turned: 0
        onWheel: wheel => {
            turned += wheel.angleDelta.y
            if (Math.abs(turned) >= 120) {
                view.shifted(turned > 0 ? -1 : 1)
                turned = 0
            }
        }
    }
}
