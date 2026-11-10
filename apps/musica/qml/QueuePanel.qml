import QtQuick

/// «A continuación»: what plays after this.
Rectangle {
    id: panel
    readonly property var player: App.player
    /// A long queue is shown by its nearest part.
    readonly property int limit: 200
    readonly property var upcoming: player.queue.slice(player.queuePosition + 1, player.queuePosition + 1 + limit)
    readonly property int remaining: Math.max(0, player.queue.length - player.queuePosition - 1)
    color: Theme.chrome

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    Text {
        id: heading
        x: 16
        y: 14
        text: qsTr("A continuación")
        color: Theme.text
        font.pixelSize: 15
        font.weight: Font.Bold
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.baseline: heading.baseline
        visible: panel.remaining > 0
        text: qsTr("Borrar")
        color: Theme.tint
        font.pixelSize: 12
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            onClicked: panel.player.clearUpcoming()
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 40
        visible: panel.remaining === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: panel.player.hasTrack ? qsTr("No hay nada más en la cola.") : qsTr("Lo que elijas reproducir aparecerá aquí.")
        color: Theme.dim
        font.pixelSize: 12
    }

    ListView {
        anchors.fill: parent
        anchors.topMargin: 44
        anchors.leftMargin: 1
        clip: true
        model: panel.upcoming
        boundsBehavior: Flickable.StopAtBounds
        footer: Text {
            visible: panel.remaining > panel.limit
            height: visible ? 34 : 8
            x: 16
            verticalAlignment: Text.AlignVCenter
            text: qsTr("y %1 más").arg(App.songs(panel.remaining - panel.limit))
            color: Theme.dim
            font.pixelSize: 12
        }
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 48
            color: hover.containsMouse ? Theme.hover : "transparent"
            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                onDoubleClicked: panel.player.jumpTo(panel.player.queuePosition + 1 + row.index)
            }
            Cover {
                id: art
                x: 14
                width: 36
                height: 36
                anchors.verticalCenter: parent.verticalCenter
                path: row.modelData.path
            }
            Column {
                anchors.left: art.right
                anchors.leftMargin: 10
                anchors.right: remove.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: row.modelData.title
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: App.shown(row.modelData.artist)
                    color: Theme.dim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            TButton {
                id: remove
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                symbol: "x"
                size: 13
                tone: Theme.dim
                opacity: hover.containsMouse ? 1 : 0
                onClicked: panel.player.removeFromQueue(panel.player.queuePosition + 1 + row.index)
            }
        }
    }
}
