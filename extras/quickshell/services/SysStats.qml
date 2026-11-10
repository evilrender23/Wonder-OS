pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// CPU (from /proc/stat deltas), memory (/proc/meminfo), load, temperature
// and the top processes. Process listing runs only while a panel asks for it.
Singleton {
    id: root

    property int cpu: 0
    property var threads: []
    property var history: []              // last 40 cpu samples
    property string load: ""
    property real memUsed: 0              // GiB
    property real memTotal: 0
    property real swapUsed: 0
    property real swapTotal: 0
    readonly property int memPercent: memTotal > 0 ? Math.round(memUsed / memTotal * 100) : 0
    property int temperature: -1
    property var topProcesses: []
    property bool wantProcesses: false
    property string sortProcessesBy: "cpu"
    property var _prev: ({})

    function _usage(name, parts) {
        const idle = parts[3] + (parts[4] || 0)
        const total = parts.reduce((a, b) => a + b, 0)
        const last = _prev[name] || { idle: idle, total: total }
        _prev[name] = { idle: idle, total: total }
        const d = total - last.total
        return d > 0 ? Math.round((1 - (idle - last.idle) / d) * 100) : 0
    }

    onWantProcessesChanged: if (wantProcesses && !ps.running) ps.running = true
    onSortProcessesByChanged: if (wantProcesses && !ps.running) ps.running = true

    function kill(pid) { Quickshell.execDetached(["kill", String(pid)]) }

    FileView {
        id: stat
        path: "/proc/stat"
        onLoaded: {
            const per = []
            for (const line of text().split("\n")) {
                const f = line.trim().split(/\s+/)
                if (!/^cpu\d*$/.test(f[0])) continue
                const v = root._usage(f[0], f.slice(1).map(Number))
                if (f[0] === "cpu") root.cpu = v
                else per.push(v)
            }
            root.threads = per
            root.history = root.history.concat([root.cpu]).slice(-40)
        }
    }

    FileView {
        id: meminfo
        path: "/proc/meminfo"
        onLoaded: {
            const kv = {}
            for (const line of text().split("\n")) {
                const m = line.match(/^(\w+):\s+(\d+)/)
                if (m) kv[m[1]] = Number(m[2])
            }
            const g = 1024 * 1024
            root.memTotal = kv.MemTotal / g
            root.memUsed = (kv.MemTotal - kv.MemAvailable) / g
            root.swapTotal = kv.SwapTotal / g
            root.swapUsed = (kv.SwapTotal - kv.SwapFree) / g
        }
    }

    FileView { id: loadavg; path: "/proc/loadavg"; onLoaded: root.load = text().split(" ").slice(0, 3).join(" · ") }

    Process {
        id: temp
        // The hottest CPU-ish thermal zone, in °C.
        command: ["sh", "-c", "for z in /sys/class/hwmon/hwmon*; do n=$(cat $z/name 2>/dev/null); case $n in k10temp|coretemp|zenpower|cpu_thermal) cat $z/temp1_input; exit;; esac; done"]
        stdout: StdioCollector { onStreamFinished: { const v = Number(text.trim()); root.temperature = v > 0 ? Math.round(v / 1000) : -1 } }
    }

    Process {
        id: ps
        command: ["ps", "-eo", "pid,pcpu,rss,comm", "--no-headers", `--sort=-${root.sortProcessesBy === "cpu" ? "pcpu" : "rss"}`]
        stdout: StdioCollector {
            onStreamFinished: {
                root.topProcesses = text.trim().split("\n").slice(0, 8).map(line => {
                    const f = line.trim().split(/\s+/)
                    return { pid: Number(f[0]), cpu: Number(f[1]), mem: Number(f[2]) / 1024, name: f.slice(3).join(" ") }
                })
            }
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            stat.reload()
            meminfo.reload()
            loadavg.reload()
            if (!temp.running) temp.running = true
            if (root.wantProcesses && !ps.running) ps.running = true
        }
    }
}
