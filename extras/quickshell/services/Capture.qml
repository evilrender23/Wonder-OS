pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Screenshots and screen recordings: what the capture overlay picks, the
// options it remembers, and the one recording that may be running. The work
// is done by extras/scripts/wonderland-capture (grim, the compositor's own
// window capture, wf-recorder or ffmpeg); this only sequences it — overlay
// away, countdown, capture, result.
Singleton {
    id: root

    property bool open: false
    property string target: "area"          // window | area | screen
    property bool record: false
    readonly property alias opts: adapter

    property bool recording: false
    property real recStarted: 0             // ms since epoch
    property var recRegion: null            // { screen, x, y, w, h } in that screen's pixels
    property int recSeconds: 0

    property var job: null                  // picked, waiting for its countdown
    property int countdown: 0
    property var result: null               // { kind, path, … } or { error }
    property string resultScreen: ""
    signal finished()

    readonly property var delays: [0, 3, 5, 10]
    readonly property var scriptCmd: ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-capture"; [ -x "$t" ] || t=wonderland-capture; exec "$t" "$@"', "sh"]

    function show(mode) {
        if (job) cancel()
        if (mode === "record") record = true
        else if (["window", "area", "screen"].includes(mode)) { target = mode; record = false }
        result = null
        open = true
    }
    function close() { open = false }
    function toggle() { if (open) close(); else show("") }

    function cycleDelay() {
        adapter.delay = delays[(delays.indexOf(adapter.delay) + 1) % delays.length]
    }

    function activeOutput() {
        const outs = Compositor.state ? Compositor.state.outputs || [] : []
        const active = outs.find(o => o.active) || outs[0]
        return active ? active.name : (Quickshell.screens[0] ? Quickshell.screens[0].name : "")
    }

    // What the overlay (or `qs ipc call capture pick`) settled on. `value`
    // is an output name, a window id, or a layout-pixel "X,Y WxH"; `region`
    // is where that sits on `screen`, for the recording frame.
    function pick(kind, value, screen, region) {
        if (runner.running || job) return
        open = false
        result = null
        job = { record: root.record, kind: kind, value: String(value), screen: screen, region: region || null }
        if (root.record && root.recording) { job = null; return }
        countdown = adapter.delay
        if (countdown > 0) tick.restart()
        // One frame for the overlay to leave the screen before it is grabbed.
        else settle.restart()
    }

    function cancel() {
        tick.stop()
        settle.stop()
        job = null
        countdown = 0
    }

    // Straight from a keybinding, no overlay: the active screen or the
    // focused window, now.
    function instant(kind) {
        const was = record
        record = false
        if (kind === "window") {
            const fs = Compositor.state ? (Compositor.state.fullscreen || []).find(f => f.output === activeOutput()) : null
            const id = fs ? fs.id : Compositor.focused ? Compositor.focused.id : -1
            if (id < 0) { result = { error: I18n.tr("No hay ninguna ventana enfocada") }; resultScreen = activeOutput(); finished() }
            else { pick("window", id, activeOutput(), null); skipDelay() }
        } else {
            pick("screen", activeOutput(), activeOutput(), null)
            skipDelay()
        }
        record = was
    }
    function skipDelay() {
        if (!job) return
        tick.stop()
        countdown = 0
        settle.restart()
    }

    function fire() {
        const j = job
        job = null
        if (!j) return
        resultScreen = j.screen
        let args
        if (j.record) {
            // A window is recorded as the rectangle it occupies on screen.
            const kind = j.kind === "window" ? "area" : j.kind
            args = ["record", "start", kind, j.value]
            if (adapter.audio) args.push("--audio")
            recRegion = j.region
        } else {
            args = ["shot", j.kind, j.value]
            if (adapter.cursor && j.kind !== "window") args.push("--cursor")
            if (!adapter.save) args.push("--no-save")
        }
        runner.command = scriptCmd.concat(args)
        runner.running = true
    }

    function stop() {
        if (!recording || runner.running) return
        runner.command = scriptCmd.concat(["record", "stop"])
        runner.running = true
    }

    function toggleRecord() {
        if (recording) stop()
        else if (open && record) close()
        else show("record")
    }

    function openFile(path) { Quickshell.execDetached(["xdg-open", path]) }
    function reveal(path) { Quickshell.execDetached(["xdg-open", path.replace(/\/[^/]*$/, "")]) }
    function copy(res) {
        if (res.kind === "shot") Quickshell.execDetached(["sh", "-c", 'wl-copy --type image/png < "$1"', "sh", res.path])
        else Quickshell.execDetached(["wl-copy", "--type", "text/uri-list", "file://" + encodeURI(res.path)])
    }
    function discard(res) {
        Quickshell.execDetached(["rm", "-f", "--", res.path])
        result = null
    }

    function handle(text, code) {
        let reply = null
        try { reply = JSON.parse(text.trim().split("\n").pop()) } catch (e) {}
        if (!reply) reply = { error: code === 0 ? I18n.tr("Respuesta inesperada") : I18n.tr("wonderland-capture no está instalado") }
        if (reply.kind === "recording") {
            recording = reply.recording === true
            if (recording) {
                recStarted = reply.started * 1000
                recSeconds = Math.max(0, Math.floor((Date.now() - recStarted) / 1000))
            } else recRegion = null
            return
        }
        if (reply.kind === "video" || (reply.error && recording && runner.command.includes("stop"))) {
            recording = false
            recRegion = null
        }
        if (reply.error && !recording) recRegion = null
        result = reply
        finished()
    }

    Process {
        id: runner
        stdout: StdioCollector { id: out }
        onExited: code => root.handle(out.text, code)
    }

    // A shell restart must not orphan a running recording's controls.
    Process {
        running: true
        command: root.scriptCmd.concat(["record", "status"])
        stdout: StdioCollector { onStreamFinished: if (text.trim()) root.handle(text, 0) }
    }

    Timer {
        id: tick
        interval: 1000
        repeat: true
        onTriggered: {
            root.countdown -= 1
            if (root.countdown <= 0) { stop(); settle.restart() }
        }
    }

    Timer { id: settle; interval: 180; onTriggered: root.fire() }

    Timer {
        interval: 1000
        repeat: true
        running: root.recording
        onTriggered: root.recSeconds = Math.max(0, Math.floor((Date.now() - root.recStarted) / 1000))
    }

    FileView {
        path: Settings.stateDir + "/capture.json"
        watchChanges: true
        atomicWrites: true
        printErrors: false
        onFileChanged: reload()
        onAdapterUpdated: writeAdapter()
        onLoadFailed: error => {
            if (error === FileViewError.FileNotFound) writeAdapter()
        }

        JsonAdapter {
            id: adapter

            property int delay: 0               // seconds before capturing
            property bool cursor: false         // draw the pointer in screenshots
            property bool audio: true           // record what the speakers play
            property bool save: true            // false = clipboard only
        }
    }
}
