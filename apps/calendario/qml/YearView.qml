import QtQuick

/// The twelve months of a year, with a dot on the days that have something.
Item {
    id: view
    property int day
    signal picked(int day)
    signal monthPicked(int day)
    signal shifted(int years)

    readonly property int year: App.parts(day).year
    readonly property int columns: width > height * 1.15 ? 4 : 3
    readonly property int rows: 12 / columns
    readonly property int cell: Math.max(14, Math.min(36, Math.floor(Math.min((width - 40) / columns - 28, 400) / 7), Math.floor(((height - 24) / rows - 62) / 6)))

    Grid {
        anchors.centerIn: parent
        columns: view.columns
        columnSpacing: Math.max(16, (view.width - 40 - view.columns * view.cell * 7) / view.columns)
        rowSpacing: Math.max(6, (view.height - 24 - view.rows * (48 + view.cell * 6)) / view.rows)
        Repeater {
            model: 12
            MiniMonth {
                required property int index
                year: view.year
                month: index + 1
                nav: false
                marks: true
                cell: view.cell
                selected: view.day
                onPicked: day => view.picked(day)
                onTitleClicked: view.monthPicked(first)
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
