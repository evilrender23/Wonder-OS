import QtQuick
import QtQuick.Controls.Basic

/// What the search found, soonest first.
Item {
    id: view
    property string text
    signal edit(var event)

    readonly property var found: {
        App.agenda.revision
        return App.agenda.search(text)
    }

    Text {
        visible: view.found.length === 0
        anchors.centerIn: parent
        text: "Ningún evento con «" + view.text.trim() + "»"
        color: Theme.dim
        font.pixelSize: 15
    }
    ListView {
        anchors.fill: parent
        anchors.topMargin: 8
        clip: true
        model: view.found
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool past: modelData.endDay < App.today
            width: ListView.view.width
            height: 52
            color: area.containsMouse ? Theme.hover : "transparent"
            Rectangle {
                x: 20
                y: 10
                width: 4
                height: parent.height - 20
                radius: 2
                color: row.modelData.color
                opacity: row.past ? 0.5 : 1
            }
            Column {
                x: 36
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 56
                spacing: 2
                Text {
                    width: parent.width
                    text: row.modelData.title
                    color: row.past ? Theme.dim : Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: {
                        let when = App.longDate(row.modelData.startDay)
                        if (!row.modelData.allDay)
                            when += " · " + row.modelData.time
                        else if (row.modelData.endDay > row.modelData.startDay)
                            when += " – " + App.mediumDate(row.modelData.endDay)
                        if (row.modelData.repeats)
                            when += " · se repite"
                        if (row.modelData.location !== "")
                            when += " · " + row.modelData.location
                        return when
                    }
                    color: Theme.dim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                x: 36
                width: parent.width - 56
                height: 1
                color: Theme.line
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                onClicked: view.edit(row.modelData)
            }
        }
    }
}
