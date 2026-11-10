import QtQuick
import QtQuick.Layouts

/// The transport, what is playing, and the volume: always at the top of the window.
Rectangle {
    id: bar
    property bool queueOpen: false
    signal queueToggled()
    signal albumRequested(string key)

    readonly property var player: App.player
    readonly property var track: player.current
    implicitHeight: 62
    color: Theme.chrome

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 14

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "shuffle"
                size: 15
                checked: bar.player.shuffle
                onClicked: bar.player.shuffle = !bar.player.shuffle
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                symbol: "prev-fill"
                size: 19
                active: bar.player.hasTrack
                onClicked: bar.player.previous()
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 38
                height: 34
                symbol: bar.player.playing ? "pause-fill" : "play-fill"
                size: 24
                active: bar.player.hasTrack
                onClicked: bar.player.toggle()
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                symbol: "next-fill"
                size: 19
                active: bar.player.canNext
                onClicked: bar.player.next()
            }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: bar.player.repeat === 2 ? "repeat-1" : "repeat"
                size: 15
                checked: bar.player.repeat !== 0
                onClicked: bar.player.cycleRepeat()
            }
        }

        // What is playing, as the display of a hi-fi.
        Rectangle {
            id: display
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            Layout.alignment: Qt.AlignCenter
            implicitHeight: 46
            radius: 7
            color: Theme.field
            
            Sym {
                anchors.centerIn: parent
                name: "music"
                size: 20
                color: Theme.faint
                visible: !bar.player.hasTrack
            }

            Cover {
                id: art
                x: 4
                y: 4
                width: 38
                height: 38
                corner: 12
                path: bar.player.currentPath
                visible: bar.player.hasTrack
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.albumRequested(bar.track.albumKey)
                }
            }
            Column {
                anchors.left: art.right
                anchors.right: parent.right
                anchors.leftMargin: 44
                anchors.rightMargin: 44
                y: 6
                spacing: 1
                visible: bar.player.hasTrack
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: bar.track.title ?? ""
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: bar.player.hasTrack ? bar.track.artist + " — " + bar.track.album : ""
                    color: Theme.dim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            Text {
                anchors.left: art.right
                anchors.leftMargin: 8
                anchors.bottom: progress.top
                anchors.bottomMargin: -1
                visible: bar.player.hasTrack
                text: App.time(progress.dragging ? progress.dragValue * bar.player.duration : bar.player.position)
                color: Theme.dim
                font.pixelSize: 10
                font.features: { "tnum": 1 }
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.bottom: progress.top
                anchors.bottomMargin: -1
                visible: bar.player.hasTrack
                text: "-" + App.time(Math.max(0, bar.player.duration - (progress.dragging ? progress.dragValue * bar.player.duration : bar.player.position)))
                color: Theme.dim
                font.pixelSize: 10
                font.features: { "tnum": 1 }
            }
            Seek {
                id: progress
                anchors.left: art.right
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 8
                anchors.leftMargin: 8
                anchors.bottomMargin: -2
                visible: bar.player.hasTrack
                value: bar.player.duration > 0 ? bar.player.position / bar.player.duration : 0
                onReleased: v => bar.player.seek(Math.round(v * bar.player.duration))
            }
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 6
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: bar.player.muted || bar.player.volume === 0 ? "volume-x" : (bar.player.volume < 0.5 ? "volume-1" : "volume-2")
                tone: Theme.dim
                onClicked: bar.player.muted = !bar.player.muted
            }
            Seek {
                anchors.verticalCenter: parent.verticalCenter
                width: 92
                value: bar.player.muted ? 0 : bar.player.volume
                onMoved: v => bar.player.volume = v
            }
            Item { width: 4; height: 1 }
            TButton {
                anchors.verticalCenter: parent.verticalCenter
                symbol: "list-music"
                checked: bar.queueOpen
                onClicked: bar.queueToggled()
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
