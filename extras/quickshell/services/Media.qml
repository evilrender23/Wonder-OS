pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Services.Mpris

// The player the bar follows: the one the user picked, else the playing one,
// else the first. Position is polled while playing (MPRIS has no tick).
Singleton {
    id: root

    readonly property var players: Mpris.players.values
    property string pinnedIdentity: ""
    readonly property var player: {
        const list = players
        const pinned = list.find(p => p.identity === pinnedIdentity)
        return pinned || list.find(p => p.isPlaying) || list[0] || null
    }
    readonly property bool present: player !== null
    readonly property bool playing: present && player.isPlaying
    readonly property string title: present ? (player.trackTitle || player.identity) : ""
    readonly property string artist: present ? (player.trackArtist || "") : ""
    readonly property string art: present ? (player.trackArtUrl || "") : ""
    property real position: 0

    function fmt(seconds) {
        if (!(seconds > 0)) return "0:00"
        const s = Math.floor(seconds % 60)
        return `${Math.floor(seconds / 60)}:${s < 10 ? "0" : ""}${s}`
    }

    Timer {
        interval: 1000
        running: root.playing
        repeat: true
        triggeredOnStart: true
        onTriggered: if (root.player) root.position = root.player.position
    }
    onPlayerChanged: position = player ? player.position : 0
}
