import QtQuick
import QtQuick.Controls.Basic

/// Days side by side with their hours down the page: a week, or one day.
Item {
    id: view
    /// The day in focus.
    property int day
    /// 7 shows its week, Monday first; 1 shows it alone.
    property int days: 7
    signal picked(int day)
    signal create(int day, int minute)
    signal edit(var event)

    readonly property int first: days === 1 ? day : day - App.parts(day).weekday
    readonly property int gutter: 62
    readonly property real column: (width - gutter) / days
    readonly property int hour: 50
    /// What each day has on top, for the whole day.
    readonly property var whole: {
        App.agenda.revision
        const lists = []
        for (let i = 0; i < days; ++i)
            lists.push(App.agenda.day(first + i).filter(event => event.allDay))
        return lists
    }
    readonly property int wholeRows: Math.min(4, Math.max(0, ...whole.map(list => list.length)))

    function scrollTo(minute) {
        hours.contentY = Math.max(0, Math.min(hours.contentHeight - hours.height, minute / 60 * hour))
    }
    Component.onCompleted: Qt.callLater(() => scrollTo(first <= App.today && App.today < first + days ? App.minute - 150 : 7 * 60))

    // --- The days' names ----------------------------------------------------

    Item {
        id: header
        width: parent.width
        height: view.days === 1 ? 0 : 54
        visible: view.days > 1
        Repeater {
            model: view.days
            Item {
                id: name
                required property int index
                readonly property int day: view.first + index
                readonly property bool today: day === App.today
                x: view.gutter + index * view.column
                width: view.column
                height: header.height
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.weekdayName(App.parts(name.day).weekday).slice(0, 3)
                        color: name.today ? Theme.tint : Theme.dim
                        font.pixelSize: 13
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30
                        height: 30
                        radius: 15
                        color: name.today ? Theme.tint : (name.day === view.day ? Theme.sideSelection : (nameArea.containsMouse ? Theme.hover : "transparent"))
                        Text {
                            anchors.centerIn: parent
                            text: App.parts(name.day).day
                            color: name.today ? "#ffffff" : Theme.text
                            font.pixelSize: 17
                            font.weight: name.today ? Font.DemiBold : Font.Normal
                        }
                    }
                }
                MouseArea {
                    id: nameArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: view.picked(name.day)
                }
            }
        }
    }

    // --- What lasts the whole day ----------------------------------------------

    Item {
        id: strip
        y: header.height
        width: parent.width
        height: view.wholeRows > 0 ? view.wholeRows * 20 + 8 : 0
        visible: height > 0
        Text {
            width: view.gutter - 8
            y: 5
            horizontalAlignment: Text.AlignRight
            text: "todo el día"
            color: Theme.faint
            font.pixelSize: 10
        }
        Repeater {
            model: view.days
            Column {
                id: stack
                required property int index
                readonly property var events: view.whole[index] ?? []
                readonly property int shown: events.length <= view.wholeRows ? events.length : view.wholeRows - 1
                x: view.gutter + index * view.column + 2
                y: 4
                width: view.column - 5
                spacing: 2
                Repeater {
                    model: stack.events.slice(0, stack.shown)
                    Chip {
                        required property var modelData
                        width: stack.width
                        reach: 3
                        untitled: stack.index > 0
                        event: modelData
                        onClicked: view.edit(modelData)
                    }
                }
                Text {
                    visible: stack.events.length > stack.shown
                    x: 5
                    height: 18
                    verticalAlignment: Text.AlignVCenter
                    text: "+" + (stack.events.length - stack.shown) + " más"
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }
    }
    Rectangle {
        y: header.height + strip.height
        width: parent.width
        height: 1
        color: Theme.border
        z: 2
    }

    // --- The hours -------------------------------------------------------------

    Flickable {
        id: hours
        y: header.height + strip.height + 1
        width: parent.width
        height: parent.height - y
        contentHeight: 24 * view.hour + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Repeater {
            model: 24
            Item {
                required property int index
                y: 6 + index * view.hour
                width: hours.width
                height: view.hour
                Text {
                    visible: parent.index > 0
                    width: view.gutter - 8
                    y: -7
                    horizontalAlignment: Text.AlignRight
                    text: parent.index + ":00"
                    color: Theme.faint
                    font.pixelSize: 11
                }
                Rectangle {
                    x: view.gutter
                    width: parent.width - x
                    height: 1
                    color: Theme.line
                }
            }
        }

        Repeater {
            model: view.days
            Item {
                id: lane
                required property int index
                readonly property int day: view.first + index
                readonly property var boxes: {
                    App.agenda.revision
                    return App.agenda.layout(day)
                }
                x: view.gutter + index * view.column
                y: 6
                width: view.column
                height: 24 * view.hour

                Rectangle {
                    anchors.fill: parent
                    color: App.parts(lane.day).weekday > 4 ? Theme.rest : "transparent"
                }
                Rectangle {
                    width: 1
                    height: parent.height
                    color: Theme.line
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: view.picked(lane.day)
                    // On the half hour under the pointer.
                    onDoubleClicked: mouse => view.create(lane.day, Math.max(0, Math.min(1410, Math.floor(mouse.y / view.hour * 2) * 30)))
                }
                Repeater {
                    model: lane.boxes
                    Rectangle {
                        id: block
                        required property var modelData
                        readonly property real slot: (lane.width - 6) / modelData.columns
                        x: 3 + modelData.column * slot
                        y: modelData.top / 60 * view.hour + 1
                        width: slot - 2
                        height: Math.max(16, (modelData.bottom - modelData.top) / 60 * view.hour - 2)
                        radius: 5
                        clip: true
                        color: blockArea.containsMouse ? Qt.alpha(modelData.color, Theme.dark ? 0.42 : 0.28) : Theme.wash(modelData.color)
                        Rectangle {
                            width: 3
                            height: parent.height
                            color: block.modelData.color
                        }
                        Column {
                            x: 8
                            y: 3
                            width: parent.width - 12
                            spacing: 0
                            Text {
                                width: parent.width
                                text: block.modelData.title
                                color: Theme.ink(block.modelData.color)
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                wrapMode: block.height > 58 ? Text.WordWrap : Text.NoWrap
                                maximumLineCount: 2
                            }
                            Text {
                                visible: block.height > 36
                                width: parent.width
                                text: block.modelData.time + " – " + block.modelData.endTime
                                color: Theme.ink(block.modelData.color)
                                opacity: 0.8
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: block.height > 76 && block.modelData.location !== ""
                                width: parent.width
                                text: block.modelData.location
                                color: Theme.ink(block.modelData.color)
                                opacity: 0.8
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: blockArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: view.edit(block.modelData)
                        }
                    }
                }
                // Now.
                Rectangle {
                    visible: lane.day === App.today
                    y: App.minute / 60 * view.hour - 1
                    width: parent.width
                    height: 2
                    color: Theme.tint
                    z: 3
                    Rectangle {
                        x: -4
                        y: -3
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.tint
                    }
                }
            }
        }
    }
}
