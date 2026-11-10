import QtQuick

/// A month in small: the sidebar's, the date fields', and each of the year's twelve.
Item {
    id: mini
    property int year: 2026
    property int month: 1
    /// The day to mark as chosen (a Julian day number), or 0.
    property int selected: 0
    /// Arrows to walk the months; without them the name alone, as a heading.
    property bool nav: true
    /// A dot under the days that have something.
    property bool marks: false
    property int cell: 28
    signal picked(int day)
    signal shifted(int months)
    signal titleClicked()

    readonly property int first: App.monthStart(year, month)
    readonly property var head: App.parts(first)
    readonly property int gridStart: first - head.weekday
    readonly property var busy: {
        App.agenda.revision
        return marks ? App.agenda.busy(gridStart, 42) : []
    }

    implicitWidth: cell * 7
    implicitHeight: 30 + 18 + cell * 6

    Text {
        id: title
        x: mini.nav ? 6 : 4
        height: 26
        verticalAlignment: Text.AlignVCenter
        text: {
            const name = App.monthName(mini.head.month)
            return name.charAt(0).toUpperCase() + name.slice(1) + (mini.nav ? " " + mini.head.year : "")
        }
        color: !mini.nav && mini.first <= App.today && App.today < mini.first + mini.head.days ? Theme.tint : Theme.text
        font.pixelSize: mini.nav ? 13 : 15
        font.weight: Font.DemiBold
        MouseArea {
            anchors.fill: parent
            cursorShape: mini.nav ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: mini.titleClicked()
        }
    }
    Row {
        visible: mini.nav
        anchors.right: parent.right
        TButton {
            width: 24
            height: 24
            size: 14
            symbol: "chevron-left"
            tone: Theme.dim
            onClicked: mini.shifted(-1)
        }
        TButton {
            width: 24
            height: 24
            size: 14
            symbol: "chevron-right"
            tone: Theme.dim
            onClicked: mini.shifted(1)
        }
    }
    Row {
        y: 30
        Repeater {
            model: ["L", "M", "X", "J", "V", "S", "D"]
            Text {
                required property string modelData
                width: mini.cell
                height: 18
                horizontalAlignment: Text.AlignHCenter
                text: modelData
                color: Theme.faint
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }
    }
    Grid {
        y: 48
        columns: 7
        Repeater {
            model: 42
            Item {
                id: spot
                required property int index
                readonly property int day: mini.gridStart + index
                readonly property int number: index - mini.head.weekday + 1
                readonly property bool inside: number >= 1 && number <= mini.head.days
                readonly property bool today: day === App.today
                width: mini.cell
                height: mini.cell
                // In the year, a month shows only its own days.
                // (Not `visible`: the grid would close the gap and shift the days.)
                opacity: mini.nav || inside ? 1 : 0
                enabled: mini.nav || inside
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) - 4
                    height: width
                    radius: width / 2
                    color: spot.today && spot.inside ? Theme.tint
                        : (spot.day === mini.selected && spot.inside ? Theme.sideSelection : (area.containsMouse ? Theme.hover : "transparent"))
                }
                Text {
                    anchors.centerIn: parent
                    text: spot.inside ? spot.number : App.parts(spot.day).day
                    color: spot.today && spot.inside ? "#ffffff" : (spot.inside ? Theme.text : Theme.faint)
                    font.pixelSize: Math.max(9, Math.min(12, mini.cell * 0.44))
                    font.weight: spot.today && spot.inside ? Font.DemiBold : Font.Normal
                }
                Rectangle {
                    visible: mini.marks && spot.inside && !spot.today && (mini.busy[spot.index] ?? 0) > 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 1
                    width: 3
                    height: 3
                    radius: 1.5
                    color: Theme.dim
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: mini.picked(spot.day)
                }
            }
        }
    }
}
