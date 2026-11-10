import QtQuick
import QtQuick.Controls.Basic

/// The list at the side: what is lined up, and how it goes round.
Rectangle {
    id: panel
    property var playlist
    signal played(int index)
    signal addRequested()
    color: Theme.panel

    // Clicks here are not clicks on the picture.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Item {
        id: header
        width: parent.width
        height: 44
        Text {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "Lista"
            color: Theme.ink
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            x: 56
            anchors.verticalCenter: parent.verticalCenter
            text: panel.playlist.count
            color: Theme.inkFaint
            font.pixelSize: 13
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            TButton {
                onStage: true
                symbol: "shuffle"
                size: 15
                checked: panel.playlist.shuffle
                tone: Theme.inkDim
                tip: "Orden aleatorio"
                onClicked: panel.playlist.shuffle = !panel.playlist.shuffle
            }
            TButton {
                onStage: true
                symbol: panel.playlist.repeat === 2 ? "repeat-1" : "repeat"
                size: 15
                checked: panel.playlist.repeat !== 0
                tone: Theme.inkDim
                tip: panel.playlist.repeat === 0 ? "Repetir la lista" : (panel.playlist.repeat === 1 ? "Repetir este" : "No repetir")
                onClicked: panel.playlist.repeat = (panel.playlist.repeat + 1) % 3
            }
            TButton {
                onStage: true
                symbol: "plus"
                size: 15
                tone: Theme.inkDim
                tip: "Añadir a la lista…"
                onClicked: panel.addRequested()
            }
        }
    }
    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        width: parent.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.playlist
        currentIndex: panel.playlist.current
        highlightFollowsCurrentItem: false
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        delegate: Item {
            id: row
            required property int index
            required property string title
            required property string folder
            required property bool current
            required property bool audio
            width: list.width
            height: 44
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                radius: 7
                color: rowArea.containsMouse || remove.hovered ? Theme.inkHover : "transparent"
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: panel.played(row.index)
            }
            Sym {
                id: glyph
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                name: row.current ? "play-fill" : (row.audio ? "music" : "film")
                size: row.current ? 12 : 14
                color: row.current ? Theme.inkTint : Theme.inkFaint
            }
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 40
                anchors.right: remove.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: row.title
                    color: row.current ? Theme.inkTint : Theme.ink
                    font.pixelSize: 13
                    font.weight: row.current ? Font.DemiBold : Font.Normal
                    elide: Text.ElideMiddle
                }
                Text {
                    width: parent.width
                    text: row.folder
                    color: Theme.inkFaint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            TButton {
                id: remove
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                onStage: true
                symbol: "x"
                size: 13
                tone: Theme.inkDim
                opacity: rowArea.containsMouse || hovered ? 1 : 0
                onClicked: panel.playlist.remove(row.index)
            }
        }
    }
}
