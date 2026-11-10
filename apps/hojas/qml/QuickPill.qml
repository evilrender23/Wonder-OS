import QtQuick

/// «Análisis rápido»: the few things a selection most likely wants, beside it.
Rectangle {
    id: pill
    required property var host
    readonly property bool range: !host.single
    readonly property bool numbers: range && host.stats.numbers > 0
    /// The room there is for it: without enough, the figures give way to the tools.
    property real room: 10000
    readonly property bool tight: room < 470
    width: row.implicitWidth + 12
    height: 34
    radius: 17
    color: Theme.menu
    border.color: Theme.border

    // A soft shadow, without effects.
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 1
        anchors.bottomMargin: -3
        radius: 19
        color: Theme.dark ? "#50000000" : "#16000000"
    }
    // Clicks on the pill are the pill's, not the sheet's.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: event => event.accepted = true }

    component Chip: Rectangle {
        id: chip
        property string label
        property string value
        signal clicked()
        anchors.verticalCenter: parent.verticalCenter
        width: chipRow.implicitWidth + 16
        height: 26
        radius: 13
        color: chipArea.containsMouse ? Theme.sideSelection : "transparent"
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            Text { text: chip.label; color: Theme.dim; font.pixelSize: 12 }
            Text { text: chip.value; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
        }
        MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; onClicked: chip.clicked() }
    }
    component Split: Item {
        width: 9
        height: 26
        anchors.verticalCenter: parent.verticalCenter
        Rectangle { anchors.centerIn: parent; width: 1; height: 16; color: Theme.border }
    }
    component Tool: TButton {
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        radius: 13
    }

    Row {
        id: row
        x: 6
        height: parent.height
        spacing: 1

        // A range of numbers: what it adds up to, written under it with a click.
        Chip { visible: pill.numbers && !pill.tight; label: "Suma"; value: pill.numbers ? pill.host.stats.sumText : ""; onClicked: pill.host.run("sum") }
        Chip { visible: pill.numbers && !pill.tight; label: "Media"; value: pill.numbers ? pill.host.stats.averageText : ""; onClicked: pill.host.run("average") }
        Chip { visible: pill.range; label: "Recuento"; value: pill.range && pill.host.stats.count !== undefined ? pill.host.stats.count : ""; onClicked: pill.host.run("count") }
        Split { visible: pill.range }
        Tool { visible: pill.range; symbol: "chart-column"; onClicked: pill.host.run("chart-column") }
        Tool { visible: pill.range; symbol: "arrow-down-a-z"; onClicked: pill.host.run("sort-asc") }
        Tool { visible: pill.range; symbol: "list-filter"; onClicked: pill.host.run("filter") }
        Tool { visible: pill.range; symbol: "palette"; onClicked: pill.host.run("conditional") }
        Tool { visible: pill.range; symbol: "table"; checked: pill.host.table.index !== undefined; onClicked: pill.host.run("table") }

        // One cell: how it looks.
        Tool { visible: !pill.range; symbol: "bold"; checked: pill.host.info.bold === true; onClicked: pill.host.run("bold") }
        Tool { visible: !pill.range; symbol: "italic"; checked: pill.host.info.italic === true; onClicked: pill.host.run("italic") }
        Tool { visible: !pill.range; symbol: "align-center"; checked: pill.host.info.align === 2; onClicked: pill.host.run("align-center") }
        Split { visible: !pill.range }
        Tool { visible: !pill.range; symbol: "euro"; checked: pill.host.info.family === "currency"; onClicked: pill.host.run("format-currency") }
        Tool { visible: !pill.range; symbol: "percent"; checked: pill.host.info.family === "percent"; onClicked: pill.host.run("format-percent") }
        Split { visible: !pill.range }
        Tool { visible: !pill.range; symbol: "copy"; onClicked: pill.host.run("copy") }
        Tool { visible: !pill.range; symbol: "panel-right"; checked: pill.host.showInspector; onClicked: pill.host.run("inspector") }
    }
}
