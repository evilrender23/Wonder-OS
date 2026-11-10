import QtQuick

/// An event in one line: in a day of the month, or along the top of a week.
Item {
    id: chip
    /// One of Agenda.day()'s.
    property var event
    /// How far past each side a bar that goes on into the next day reaches.
    property real reach: 0
    readonly property bool fromBefore: event.allDay && !event.begins && reach > 0
    readonly property bool goesOn: event.allDay && !event.ends && reach > 0
    /// A bar that comes from the day before says its name only once.
    property bool untitled: false
    signal clicked()
    height: 18
    Rectangle {
        x: chip.fromBefore ? -chip.reach : 0
        width: chip.width - x + (chip.goesOn ? chip.reach + 1 : 0)
        height: parent.height
        topLeftRadius: chip.fromBefore ? 0 : 4
        bottomLeftRadius: chip.fromBefore ? 0 : 4
        topRightRadius: chip.goesOn ? 0 : 4
        bottomRightRadius: chip.goesOn ? 0 : 4
        color: chip.event.allDay ? (area.containsMouse ? Qt.darker(chip.event.color, 1.1) : chip.event.color) : (area.containsMouse ? Theme.hover : "transparent")
    }
    Rectangle {
        id: dot
        visible: !chip.event.allDay
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 7
        height: 7
        radius: 3.5
        color: chip.event.color
    }
    Text {
        id: hour
        visible: !chip.event.allDay && chip.width > 96
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: chip.event.begins ? chip.event.time : ""
        color: Theme.dim
        font.pixelSize: 11
    }
    Text {
        anchors.left: parent.left
        anchors.leftMargin: chip.event.allDay ? 6 : 15
        anchors.right: hour.visible ? hour.left : parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: chip.untitled && chip.fromBefore ? "" : chip.event.title
        color: chip.event.allDay ? "#ffffff" : Theme.text
        font.pixelSize: 12
        font.weight: chip.event.allDay ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: chip.clicked()
    }
}
