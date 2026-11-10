pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// First backlight under /sys, read directly; set through brightnessctl (as
// the brightness keys do) with a 50 ms coalescing window while dragging.
Singleton {
    id: root

    property string backlight: ""
    property int maximum: 0
    property int current: 0
    readonly property bool present: backlight !== "" && maximum > 0
    readonly property real fraction: present ? current / maximum : 0
    readonly property bool canSet: hasTool
    property bool hasTool: false

    function set(percent) {
        setter.pending = Math.max(1, Math.min(100, percent))
        setter.restart()
    }

    Process {
        command: ["sh", "-c", "ls /sys/class/backlight | head -n1; command -v brightnessctl >/dev/null && echo tool"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = text.trim().split("\n")
                root.hasTool = lines.includes("tool")
                const name = lines.find(l => l && l !== "tool")
                if (name) root.backlight = `/sys/class/backlight/${name}`
            }
        }
    }

    FileView { path: root.backlight ? `${root.backlight}/max_brightness` : ""; onLoaded: root.maximum = Number(text().trim()) }
    FileView { id: level; path: root.backlight ? `${root.backlight}/brightness` : ""; onLoaded: root.current = Number(text().trim()) }
    Timer { interval: 3000; running: root.present; repeat: true; onTriggered: level.reload() }

    Timer {
        id: setter
        property real pending: 0
        interval: 50
        onTriggered: {
            if (writer.running) { restart(); return }
            root.current = Math.round(pending / 100 * root.maximum)
            writer.command = ["brightnessctl", "-q", "set", `${Math.round(pending)}%`]
            writer.running = true
        }
    }
    Process { id: writer; onExited: level.reload() }
}
