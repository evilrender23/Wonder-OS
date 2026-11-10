pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Formatting in another timezone. The QML JS engine has no Intl time zones,
// so `date` reports each zone's UTC offset (refreshed hourly, which covers DST
// changes) and times are shifted by the difference from local time.
Singleton {
    id: root

    readonly property var zones: ["UTC", "Europe/Madrid", "Europe/London", "America/New_York", "America/Los_Angeles", "America/Mexico_City", "America/Buenos_Aires", "Asia/Tokyo", "Asia/Shanghai", "Australia/Sydney"]
    property var offsets: ({})          // zone -> minutes east of UTC

    function format(date, fmt, zone) {
        const tz = zone === undefined ? Settings.data.timezone : zone
        if (!tz || !(tz in offsets)) return date.toLocaleString(Qt.locale(), fmt)
        const localOffset = -date.getTimezoneOffset()
        const shifted = new Date(date.getTime() + (offsets[tz] - localOffset) * 60000)
        return shifted.toLocaleString(Qt.locale(), fmt)
    }

    function refresh() {
        const list = Array.from(new Set(zones.concat(Settings.data.timezone ? [Settings.data.timezone] : [])))
        probe.command = ["sh", "-c", 'for z in "$@"; do printf "%s %s\\n" "$z" "$(TZ=$z date +%z)"; done', "sh"].concat(list)
        probe.running = true
    }

    Component.onCompleted: refresh()

    Process {
        id: probe
        stdout: StdioCollector {
            onStreamFinished: {
                const map = {}
                for (const line of text.trim().split("\n")) {
                    const m = line.match(/^(\S+) ([+-])(\d\d)(\d\d)$/)
                    if (m) map[m[1]] = (m[2] === "-" ? -1 : 1) * (Number(m[3]) * 60 + Number(m[4]))
                }
                root.offsets = map
            }
        }
    }

    Timer { interval: 3600000; running: true; repeat: true; onTriggered: root.refresh() }
    Connections { target: Settings.data; function onTimezoneChanged() { root.refresh() } }
}
