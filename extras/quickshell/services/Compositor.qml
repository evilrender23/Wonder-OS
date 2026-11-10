pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// The compositor over its IPC socket (docs/ipc.md): one connection subscribed
// to whole-state snapshots, one for request/reply. Replies arrive in request
// order, so callbacks are a FIFO. Both reconnect on their own, so restarting
// the compositor or starting the bar first is harmless.
Singleton {
    id: root

    readonly property string socketPath: Quickshell.env("DRIFTWM_SOCKET")
        || `${Quickshell.env("XDG_RUNTIME_DIR")}/driftwm/ipc-${Quickshell.env("WAYLAND_DISPLAY")}.sock`

    readonly property bool connected: events.connected
    property var state: null
    property var bookmarks: ({})            // name -> [x, y] (Y-up canvas coords)
    readonly property var windows: state ? state.windows.filter(w => !w.is_widget) : []
    readonly property var focused: windows.find(w => w.is_focused) || null
    readonly property real zoom: state ? state.zoom : 1
    readonly property string activeBookmark: state && state.active_bookmark ? state.active_bookmark : ""
    readonly property string layoutShort: state ? state.layout_short || "" : ""
    readonly property var bookmarkNames: Object.keys(bookmarks).sort((a, b) => {
        const na = Number(a), nb = Number(b)
        if (!isNaN(na) && !isNaN(nb)) return na - nb
        return a.localeCompare(b)
    })

    property var _pending: []

    function request(obj, callback) {
        _pending.push(callback || null)
        requests.write(JSON.stringify(obj) + "\n")
        requests.flush()
    }

    function action(cmd) {
        request({ Action: cmd })
    }

    function refreshBookmarks() {
        request({ Bookmark: {} }, reply => {
            if (reply.Ok && reply.Ok.Bookmarks) root.bookmarks = reply.Ok.Bookmarks
        })
    }

    function goToBookmark(name) { action(`go-to-bookmark ${name}`) }
    function setBookmark(name) { action(`set-bookmark ${name}`); refreshBookmarks() }
    function deleteBookmark(name) { request({ Bookmark: { name: name, delete: true } }); refreshBookmarks() }
    function focusWindow(id) { request({ Focus: id }) }
    function relaunch(id) { request({ Relaunch: id }) }
    function closeWindow(id) { request({ Close: id }) }
    function suspendWindow(id) { request({ Suspend: id }) }
    function minimizeWindow(id) { request({ Minimize: { window: id, value: true } }) }
    function restoreWindow(id) { request({ Minimize: { window: id, value: false } }) }
    function setZoom(z) { request({ Zoom: z }) }

    Socket {
        id: events
        path: root.socketPath
        connected: true
        onConnectionStateChanged: {
            if (connected) {
                write("\"Subscribe\"\n")
                flush()
                root.refreshBookmarks()
            } else {
                reconnect.restart()
            }
        }
        parser: SplitParser {
            onRead: line => {
                let msg
                try { msg = JSON.parse(line) } catch (e) { return }
                if (msg.State) {
                    // The active bookmark changing is the cheap hint that the
                    // set may have changed too (set-bookmark from a key).
                    const prev = root.state ? root.state.active_bookmark : null
                    root.state = msg.State
                    if (msg.State.active_bookmark && !(msg.State.active_bookmark in root.bookmarks))
                        root.refreshBookmarks()
                    else if (prev !== msg.State.active_bookmark && msg.State.active_bookmark === null)
                        root.refreshBookmarks()
                }
            }
        }
    }

    Socket {
        id: requests
        path: root.socketPath
        connected: true
        onConnectionStateChanged: {
            if (!connected) {
                root._pending = []
                reconnect.restart()
            }
        }
        parser: SplitParser {
            onRead: line => {
                const cb = root._pending.shift()
                if (!cb) return
                try { cb(JSON.parse(line)) } catch (e) { console.warn("canvas ipc:", e) }
            }
        }
    }

    Timer {
        id: reconnect
        interval: 2000
        onTriggered: {
            if (!events.connected) { events.connected = false; events.connected = true }
            if (!requests.connected) { requests.connected = false; requests.connected = true }
            if (!events.connected || !requests.connected) restart()
        }
    }

    Timer {
        // Bookmarks set over IPC by other tools leave no trace in the
        // subscribe stream; a slow poll keeps the list honest.
        interval: 15000
        running: root.connected
        repeat: true
        onTriggered: root.refreshBookmarks()
    }
}
