import QtQuick
import QtMultimedia

/// Quick Look's player: a song or a film starts at once, with a bar to pause and seek.
Item {
    id: media
    property url source
    property bool video: false
    property string icon
    /// The film's own size, once known.
    signal shaped(size size)
    signal broken()

    function clock(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        const s = seconds % 60
        const m = Math.floor(seconds / 60) % 60
        const h = Math.floor(seconds / 3600)
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0")
    }
    function toggle() {
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else
            player.play()
    }
    onSourceChanged: Qt.callLater(() => {
        if (String(media.source) !== "")
            player.play()
    })

    MediaPlayer {
        id: player
        source: media.source
        audioOutput: AudioOutput {
            muted: App.testing
        }
        videoOutput: screen
        onErrorOccurred: media.broken()
        onMetaDataChanged: {
            const size = metaData.value(MediaMetaData.Resolution)
            if (size && size.width > 0)
                media.shaped(size)
        }
    }
    Rectangle {
        visible: media.video
        anchors.fill: screen
        color: "black"
    }
    VideoOutput {
        id: screen
        visible: media.video
        anchors.fill: parent
        anchors.bottomMargin: bar.height
    }
    Image {
        visible: !media.video
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -bar.height / 2
        width: 144
        height: 144
        sourceSize: Qt.size(288, 288)
        source: media.icon === "" ? "" : App.icons + media.icon
    }
    MouseArea {
        anchors.fill: parent
        anchors.bottomMargin: bar.height
        onClicked: media.toggle()
    }

    Item {
        id: bar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 38

        TButton {
            id: playButton
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            symbol: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
            onClicked: media.toggle()
        }
        Text {
            id: elapsed
            anchors.left: playButton.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: media.clock(player.position)
            color: Theme.dim
            font.pixelSize: 11
        }
        Text {
            id: total
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: media.clock(player.duration)
            color: Theme.dim
            font.pixelSize: 11
        }
        Rectangle {
            id: track
            anchors.left: elapsed.right
            anchors.leftMargin: 10
            anchors.right: total.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: 4
            radius: 2
            color: Theme.field
            Rectangle {
                width: player.duration > 0 ? parent.width * player.position / player.duration : 0
                height: parent.height
                radius: 2
                color: Theme.accent
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                enabled: player.seekable
                function seek(mouse) {
                    player.position = Math.max(0, Math.min(1, (mouse.x - 8) / track.width)) * player.duration
                }
                onPressed: mouse => seek(mouse)
                onPositionChanged: mouse => seek(mouse)
            }
        }
    }
}
