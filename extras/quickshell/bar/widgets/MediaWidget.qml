import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Mpris
import "../.."
import "../../services"
import "../../components"

// Inline controls and the track; right click opens the media panel (art,
// seek, player picker). Style "full" (Settings.data.mediaStyle) adds a
// progress line under the title. Scroll: previous/next.
Item {
    id: root

    property var bar
    property string widgetId: "media"
    readonly property bool shown: Media.present
    readonly property bool full: Settings.data.mediaStyle === "full"

    implicitWidth: Media.present ? row.implicitWidth + 16 : 0
    implicitHeight: Theme.pillHeight

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: mouse.containsMouse || panel.visible ? Theme.p.hover : "transparent"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: event => {
            if (event.button === Qt.RightButton || !Media.player.canTogglePlaying) panel.toggle()
            else panel.toggle()
        }
        onWheel: event => {
            if (!Media.player) return
            if (event.angleDelta.y > 0 && Media.player.canGoPrevious) Media.player.previous()
            else if (event.angleDelta.y < 0 && Media.player.canGoNext) Media.player.next()
        }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            implicitWidth: 18; implicitHeight: 18
            radius: 4
            color: Theme.p.track
            clip: true
            Image { anchors.fill: parent; source: Media.art; fillMode: Image.PreserveAspectCrop; visible: Media.art !== "" }
            Icon { anchors.centerIn: parent; visible: Media.art === ""; name: "music"; size: 12 }
        }

        ColumnLayout {
            spacing: 0
            Label {
                Layout.maximumWidth: 200
                text: Media.artist ? `${Media.title} · ${Media.artist}` : Media.title
            }
            Rectangle {
                visible: root.full
                Layout.fillWidth: true
                implicitHeight: 2
                radius: 1
                color: Theme.p.track
                Rectangle {
                    width: Media.present && Media.player.length > 0 ? parent.width * Media.position / Media.player.length : 0
                    height: 2
                    radius: 1
                    color: Theme.p.accent
                }
            }
        }

        IconButton { icon: "skip-back"; size: 22; enabled: Media.present && Media.player.canGoPrevious; onClicked: Media.player.previous() }
        IconButton { icon: Media.playing ? "pause" : "play"; size: 22; checked: Media.playing; onClicked: Media.player.togglePlaying() }
        IconButton { icon: "skip-forward"; size: 22; enabled: Media.present && Media.player.canGoNext; onClicked: Media.player.next() }
    }

    BarPanel {
        id: panel
        target: root
        title: Media.present ? Media.player.identity : "Media"
        icon: "music"
        contentWidth: 320

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                implicitWidth: 96; implicitHeight: 96
                radius: Theme.popupRadius
                color: Theme.p.track
                clip: true
                Image { anchors.fill: parent; source: Media.art; fillMode: Image.PreserveAspectCrop; visible: Media.art !== "" }
                Icon { anchors.centerIn: parent; visible: Media.art === ""; name: "music"; size: 32 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { Layout.fillWidth: true; role: "title"; text: Media.title; wrapMode: Text.Wrap; maximumLineCount: 2 }
                Label { Layout.fillWidth: true; role: "muted"; text: Media.artist }
                Label { Layout.fillWidth: true; role: "caption"; text: Media.present ? (Media.player.trackAlbum || "") : "" }
            }
        }

        ThinSlider {
            id: seek
            Layout.fillWidth: true
            enabled: Media.present && Media.player.canSeek && Media.player.length > 0
            from: 0
            to: Media.present && Media.player.length > 0 ? Media.player.length : 1
            onMoved: Media.player.position = value
        }
        Binding { target: seek; property: "value"; value: Media.position; when: !seek.pressed; restoreMode: Binding.RestoreNone }
        RowLayout {
            Layout.fillWidth: true
            Label { role: "caption"; text: Media.fmt(seek.value) }
            Item { Layout.fillWidth: true }
            Label { role: "caption"; text: Media.present ? Media.fmt(Media.player.length) : "" }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            IconButton { icon: "skip-back"; size: 34; enabled: Media.present && Media.player.canGoPrevious; onClicked: Media.player.previous() }
            IconButton { icon: Media.playing ? "pause" : "play"; size: 40; primary: true; onClicked: Media.player.togglePlaying() }
            IconButton { icon: "skip-forward"; size: 34; enabled: Media.present && Media.player.canGoNext; onClicked: Media.player.next() }
        }

        Divider { visible: Media.players.length > 1 }
        Repeater {
            model: Media.players.length > 1 ? Media.players : []
            delegate: ListItem {
                required property var modelData
                icon: modelData.isPlaying ? "play" : "pause"
                text: modelData.identity
                detail: modelData.trackTitle || ""
                highlighted: modelData === Media.player
                onClicked: Media.pinnedIdentity = modelData.identity
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { role: "caption"; text: "Bar style"; Layout.fillWidth: true }
            IconButton { text: "default"; size: 24; checked: !root.full; onClicked: Settings.data.mediaStyle = "default" }
            IconButton { text: "full"; size: 24; checked: root.full; onClicked: Settings.data.mediaStyle = "full" }
        }
    }
}
