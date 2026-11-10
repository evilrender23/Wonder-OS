pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// The focused app's own menu bar (Archivo, Editar…), for the bar's `appmenu`
// widget. Apps that export their menus over D-Bus (Qt/KDE apps under the KDE
// platform theme) hand them to extras/scripts/wonderland-appmenu, which this
// service runs and talks to in JSON lines; see that script for the protocol.
// Apps that don't export a menu leave `items` empty.
Singleton {
    id: root

    readonly property var scriptCmd: ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-appmenu"; [ -x "$t" ] || t=wonderland-appmenu; exec "$t"']

    // The window the menus belong to: the focused one, or the last one focused
    // while it is still open (a popup grab can blank the focus flag).
    property var target: null
    readonly property var focused: Compositor.focused
    onFocusedChanged: if (focused) target = focused
    readonly property bool hasWindow: target !== null && Compositor.windows.some(w => w.id === target.id)

    property int window: -1            // compositor id `items` belongs to
    property var topItems: []          // [{ id, label }]
    property var submenus: ({})        // id -> [{ id, label, shortcut, children… }]
    readonly property var items: hasWindow && window === target.id ? topItems : []

    signal showRequested(int index)

    // What the bridge needs to know; sent again only when it changes.
    readonly property string focusKey: {
        if (!hasWindow || !target.pid) return ""
        const ids = Compositor.windows.filter(w => w.pid === target.pid).map(w => w.id).sort((a, b) => a - b)
        return JSON.stringify({ cmd: "focus", window: target.id, pid: target.pid, windows: ids })
    }
    onFocusKeyChanged: sendFocus()

    function send(obj) {
        if (!bridge.running) return
        bridge.write((typeof obj === "string" ? obj : JSON.stringify(obj)) + "\n")
    }
    function sendFocus() { send(focusKey !== "" ? focusKey : { cmd: "focus" }) }

    function open(id) { send({ cmd: "open", id: id }) }
    function close() { send({ cmd: "close" }) }
    function activate(id) { send({ cmd: "activate", id: id }) }

    // Children of a submenu: what the app sent when it was opened, else what
    // came with its parent.
    function childrenOf(item) { return submenus[item.id] || item.children || [] }

    Process {
        id: bridge
        command: root.scriptCmd
        running: true
        stdinEnabled: true
        onStarted: root.sendFocus()
        onExited: { root.topItems = []; root.submenus = ({}); restart.restart() }
        stdout: SplitParser {
            onRead: line => {
                let msg
                try { msg = JSON.parse(line) } catch (e) { return }
                if (msg.event === "menu") {
                    if (msg.window !== root.window) root.submenus = ({})
                    root.window = msg.window === null || msg.window === undefined ? -1 : msg.window
                    root.topItems = msg.items || []
                } else if (msg.event === "submenu") {
                    const next = Object.assign({}, root.submenus)
                    next[msg.id] = msg.items || []
                    root.submenus = next
                }
            }
        }
    }

    Timer { id: restart; interval: 5000; onTriggered: bridge.running = true }

    // `qs ipc call appmenu …`: what the bar shows, and the same actions a
    // click runs.
    IpcHandler {
        target: "appmenu"
        function list(): string { return JSON.stringify({ window: root.window, items: root.items }) }
        function submenu(id: int): string { return JSON.stringify(root.submenus[id] || []) }
        function menu(index: int): void { root.showRequested(index) }
        function open(id: int): void { root.open(id) }
        function activate(id: int): void { root.activate(id) }
    }
}
