pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// NetworkManager through nmcli (the Quickshell.Networking module doesn't
// flag saved or connected networks).
// `nmcli monitor` triggers refreshes; Wi-Fi scans run only while a panel
// sets `scanning`.
Singleton {
    id: root

    property bool available: false
    property bool hasWifi: false
    property bool radio: false
    property bool wired: false
    property string wiredDevice: ""
    property string ssid: ""
    property int signal: 0
    property var networks: []
    property string busy: ""
    property string error: ""
    property bool scanning: false
    property bool _queued: false
    property bool _queuedRescan: false

    readonly property string icon: wired ? "network"
        : !hasWifi || !radio ? "wifi-off"
        : !ssid ? "wifi-off"
        : signal > 66 ? "wifi" : signal > 33 ? "wifi-high" : "wifi-low"
    readonly property string summary: wired ? "Wired" : ssid ? ssid : radio ? "Disconnected" : "Wi-Fi off"

    onScanningChanged: if (scanning) refresh(true)

    function refresh(rescan) {
        if (refresher.running) {
            _queued = true
            _queuedRescan = _queuedRescan || rescan
            return
        }
        refresher.command = ["sh", "-c", `nmcli radio wifi; echo @@; nmcli -t -e no -f TYPE,STATE,DEVICE dev; echo @@; nmcli -t -e no -f TYPE,NAME con show; echo @@; nmcli -t -e no -f ACTIVE,SIGNAL,SECURITY,SSID dev wifi list --rescan ${rescan ? "yes" : "no"}`]
        refresher.running = true
    }

    function parse(text) {
        const sections = text.split(/^@@$/m)
        if (sections.length < 4) return
        const [radioText, devices, connections, list] = sections
        radio = radioText.trim() === "enabled"
        let wifi = false, cable = false, cableDev = ""
        for (const line of devices.trim().split("\n")) {
            const [type, state, name] = line.split(":")
            if (type === "wifi") wifi = true
            else if (type === "ethernet" && state && state.startsWith("connected")) { cable = true; cableDev = name }
        }
        hasWifi = wifi
        wired = cable
        wiredDevice = cableDev
        const known = new Set()
        for (const line of connections.trim().split("\n")) {
            const type = line.substring(0, line.indexOf(":"))
            if (type === "802-11-wireless") known.add(line.substring(type.length + 1))
        }
        const bySsid = new Map()
        for (const line of list.trim().split("\n")) {
            const parts = line.split(":")
            if (parts.length < 4) continue
            const entry = { active: parts[0] === "yes", signal: Number(parts[1]), security: parts[2], ssid: parts.slice(3).join(":") }
            if (!entry.ssid) continue
            entry.known = known.has(entry.ssid)
            const seen = bySsid.get(entry.ssid)
            if (!seen || entry.active || (!seen.active && entry.signal > seen.signal)) bySsid.set(entry.ssid, entry)
        }
        const next = [...bySsid.values()].sort((a, b) => (b.active - a.active) || (b.known - a.known) || (b.signal - a.signal))
        if (JSON.stringify(next) !== JSON.stringify(networks)) networks = next
        const active = next.find(n => n.active)
        ssid = active ? active.ssid : ""
        signal = active ? active.signal : 0
    }

    function run(label, command) {
        if (runner.running) return
        error = ""
        busy = label
        runner.label = label
        runner.command = command
        runner.running = true
    }

    function needsPassword(n) {
        return !n.active && !n.known && n.security !== "" && !n.security.includes("OWE")
    }
    function connect(n, password) {
        if (n.active) run("Disconnecting", ["nmcli", "connection", "down", "id", n.ssid])
        else if (password) run(`Joining ${n.ssid}`, ["nmcli", "device", "wifi", "connect", n.ssid, "password", password])
        else run(`Joining ${n.ssid}`, ["nmcli", "device", "wifi", "connect", n.ssid])
    }
    function forget(n) { run(`Forgetting ${n.ssid}`, ["nmcli", "connection", "delete", "id", n.ssid]) }
    function setRadio(on) { run("Switching Wi-Fi", ["nmcli", "radio", "wifi", on ? "on" : "off"]) }

    Process {
        id: refresher
        stdout: StdioCollector { onStreamFinished: root.parse(text) }
        onExited: exitCode => {
            root.available = exitCode === 0
            if (root.available && !monitor.running) monitor.running = true
            if (root._queued) {
                const rescan = root._queuedRescan
                root._queued = false
                root._queuedRescan = false
                root.refresh(rescan)
            }
        }
    }

    Process {
        id: runner
        property string label
        stdout: StdioCollector {}
        stderr: StdioCollector { id: runnerErr }
        onExited: exitCode => {
            if (exitCode !== 0) root.error = runnerErr.text.trim().split("\n").pop() || `${label} failed`
            root.busy = ""
            root.refresh(root.scanning)
        }
    }

    Process {
        id: monitor
        command: ["sh", "-c", "exec 3<&0; nmcli monitor & mon=$!; { read -r _ <&3; kill $mon 2>/dev/null; } & wait $mon; kill $! 2>/dev/null"]
        stdinEnabled: true
        stdout: SplitParser { onRead: debounce.restart() }
        onExited: retry.restart()
    }

    Timer { id: retry; interval: 5000; onTriggered: if (root.available && !monitor.running) monitor.running = true }
    Timer { id: debounce; interval: 800; onTriggered: root.refresh(false) }
    Timer {
        interval: root.scanning ? 10000 : 30000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refresh(root.scanning)
    }
}
