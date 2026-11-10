pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// What Wonderland Finder is copying, moving or deleting, for the bar's
// progress indicator. The Finder writes its state to a file in the runtime
// directory (apps/finder/src/opsreport.cpp) and rewrites it every second while
// it works, so a state that stops being refreshed belongs to a Finder that died.
Singleton {
    id: root

    property bool busy: false
    property string kind: ""        // copy, move, trash, delete, other
    property string status: ""      // "Copying…", in the Finder's language
    property real progress: 0       // 0–1; negative: amount unknown
    property string detail: ""      // "1.2 GB of 4 GB"
    property real updated: 0

    // Short operations never show; one that showed stays a moment at 100 %.
    property bool active: false

    readonly property string dir: `${Quickshell.env("XDG_RUNTIME_DIR")}/wonderland`
    readonly property int staleMs: 5000

    function load(json) {
        let state
        try {
            state = JSON.parse(json)
        } catch (e) {
            // Caught half written; the next change notification brings the rest.
            return
        }
        updated = state.updated || 0
        const live = state.busy === true && Date.now() - updated < staleMs
        if (live) {
            kind = state.kind || "other"
            status = state.status || ""
            progress = state.progress === undefined ? -1 : state.progress
            detail = state.detail || ""
        }
        busy = live
    }

    onBusyChanged: {
        if (busy) {
            linger.stop()
            if (!active) appear.restart()
        } else {
            appear.stop()
            if (active) {
                progress = 1
                detail = ""
                linger.restart()
            }
        }
    }

    Timer { id: appear; interval: 350; onTriggered: root.active = root.busy }
    Timer { id: linger; interval: 700; onTriggered: root.active = false }
    Timer {
        interval: 1000
        running: root.busy
        repeat: true
        onTriggered: if (Date.now() - root.updated > root.staleMs) root.busy = false
    }

    // The file has to exist before it can be watched.
    Process {
        running: true
        command: ["sh", "-c", 'mkdir -p "$1" && { [ -e "$1/finder-ops.json" ] || echo "{}" > "$1/finder-ops.json"; }', "sh", root.dir]
        onExited: view.path = `${root.dir}/finder-ops.json`
    }

    FileView {
        id: view
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.load(text())
    }
}
