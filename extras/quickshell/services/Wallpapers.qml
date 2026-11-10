pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Live wallpapers (extras/wallpapers/live) as the bar sees them: the list
// with each one's settings schema, what is applied, and preview/apply/cancel/
// restore through extras/scripts/wonderland-wallpaper, which renders the
// shader and points the compositor's [background] at it (driftwm hot-reloads
// it). A preview is on screen but not kept: `cancel` goes back to what was
// applied, `apply` keeps it. Calls are serialized; previews that pile up
// behind a running one collapse into the newest.
//
// The list also carries the user's own pictures (`kind: "image"`):
// `importImages()` opens a file dialog and copies what is chosen, `remove()`
// deletes one.
Singleton {
    id: root

    property var list: []            // manifests, params include the globals
    property var state: ({ current: null, settings: {} })
    property bool loaded: false
    property string error: ""
    readonly property string current: state && state.current ? state.current : ""
    // A preview is on screen (or on its way there).
    property bool previewing: false
    // The file dialog of `importImages()` is open, or its pictures are being
    // copied.
    property bool importing: false
    // Why the last import brought nothing, or left pictures out.
    property string importError: ""
    // Ids of the pictures an import added, once they are in `list`.
    signal imported(var ids)

    readonly property var scriptCmd: ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-wallpaper"; [ -x "$t" ] || t=wonderland-wallpaper; exec "$t" "$@"', "sh"]

    function byId(id) { return list.find(w => w.id === id) || null }

    // Stored values for `id`, completed with the defaults of its schema.
    function values(id) {
        const w = byId(id)
        if (!w) return {}
        const stored = (state.settings || {})[id] || {}
        const out = {}
        for (const p of w.params) out[p.key] = stored[p.key] !== undefined ? stored[p.key] : p.default
        return out
    }

    function refresh() { lister.running = true }

    property var queue: []
    function enqueue(cmd) {
        const q = queue.slice()
        const last = q.length ? q[q.length - 1][0] : ""
        // A newer preview (or its cancel) makes a waiting one pointless.
        if (last === "preview" && (cmd[0] === "preview" || cmd[0] === "cancel")) q.pop()
        q.push(cmd)
        queue = q
        pump()
    }

    // Show `id` with `settings` over its stored ones, without keeping either.
    function preview(id, settings) {
        previewing = true
        enqueue(["preview", id, JSON.stringify(settings || {})])
    }
    function cancel() {
        if (!previewing) return
        previewing = false
        enqueue(["cancel"])
    }
    function apply(id, settings) {
        previewing = false
        // Optimistic: the panel shows the new values right away.
        const s = Object.assign({}, state)
        s.current = id
        s.settings = Object.assign({}, s.settings || {})
        s.settings[id] = Object.assign({}, values(id), settings || {})
        state = s
        enqueue(["apply", id, JSON.stringify(settings || {})])
    }
    function restore() { previewing = false; enqueue(["restore"]) }

    // Ask for pictures and add them to the list.
    function importImages() {
        if (importing) return
        importError = ""
        importing = true
        importer.running = true
    }
    // Delete an imported picture; if it is the applied one, the background
    // from before the managed wallpapers comes back.
    function remove(id) {
        if (previewing) { previewing = false; enqueue(["cancel"]) }
        enqueue(["remove", id])
    }

    function pump() {
        if (runner.running || !queue.length) return
        const q = queue.slice()
        const cmd = q.shift()
        runner.relist = cmd[0] === "remove"
        runner.command = scriptCmd.concat(cmd)
        queue = q
        runner.running = true
    }

    Process {
        id: runner
        // The command changed the list, not just the state.
        property bool relist: false
        stderr: StdioCollector { id: runErr }
        onExited: code => {
            root.error = code === 0 ? "" : (runErr.text.trim().split("\n").slice(-2).join("\n") || I18n.tr("wonderland-wallpaper no está instalado"))
            if (relist) root.refresh()
            if (root.queue.length) root.pump()
            else stateReader.running = true
        }
    }

    Process {
        id: stateReader
        command: root.scriptCmd.concat(["current"])
        stdout: StdioCollector {
            onStreamFinished: {
                try { root.state = JSON.parse(text) } catch (e) {}
            }
        }
    }

    Process {
        id: importer
        command: root.scriptCmd.concat(["pick"])
        // Ids waiting for the list that has them.
        property var pending: []
        stdout: StdioCollector { id: importOut }
        stderr: StdioCollector { id: importErr }
        onExited: code => {
            let data = null
            try { data = JSON.parse(importOut.text) } catch (e) {}
            if (!data) {
                root.importError = importErr.text.trim().split("\n").pop().replace(/^wonderland-wallpaper: /, "")
                    || I18n.tr("No se pudo importar la imagen")
                root.importing = false
                return
            }
            const errors = data.errors || []
            root.importError = errors.length ? I18n.tr("No se pudo importar %1").arg(errors.join("; ")) : ""
            pending = data.imported || []
            if (pending.length) root.refresh()
            else root.importing = false
        }
    }

    Process {
        id: lister
        command: root.scriptCmd.concat(["list"])
        stdout: StdioCollector {
            onStreamFinished: {
                let data
                try { data = JSON.parse(text) } catch (e) { root.error = I18n.tr("No se pudo leer la lista de fondos"); return }
                root.list = data.wallpapers || []
                root.state = data.state || { current: null, settings: {} }
                root.loaded = true
                if (importer.pending.length) {
                    const ids = importer.pending
                    importer.pending = []
                    root.importing = false
                    root.imported(ids)
                }
                // A preview nobody is driving (the bar died mid-preview)
                // would stay on screen for good: undo it.
                if (root.state.preview && !root.previewing && !runner.running && !root.queue.length) {
                    root.previewing = true
                    root.cancel()
                }
            }
        }
        onExited: code => {
            if (code === 0) return
            root.error = I18n.tr("wonderland-wallpaper no está instalado")
            importer.pending = []
            root.importing = false
        }
    }

    Component.onCompleted: refresh()
}
