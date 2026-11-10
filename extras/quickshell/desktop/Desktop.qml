import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import ".."
import "../services"
import "magnet.js" as Magnet

// The Desktop folder as icons on the primary screen, placed freely as on
// macOS: an icon stays where it is dropped, and while it is dragged its
// neighbours and the screen margins pull it into line (magnet.js).
//
// The surface covers the screen under the windows, but only the icons take
// input, so the canvas keeps its own clicks and drags everywhere else.
// While a file manager drags files (`appDrag` over IPC) the whole surface
// takes the drop, and the files move to the Desktop folder.
// extras/scripts/wonderland-desktop lists and watches the folder and does the
// file operations. `qs ipc call desktop <fn>` drives it from keybindings.
Scope {
    id: root

    readonly property var primaryScreen: Quickshell.screens.find(s => s.x === 0 && s.y === 0) || Quickshell.screens[0]
    readonly property var scriptCmd: ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-desktop"; [ -x "$t" ] || t=wonderland-desktop; exec "$t" "$@"', "sh"]

    readonly property int iconSize: Math.max(32, Math.min(96, Settings.data.desktopIconSize))
    readonly property int cellW: iconSize + 52
    readonly property int cellH: iconSize + 58
    property real fieldWidth: 0
    property real fieldHeight: 0
    // Shift held during a drag switches the magnet off for that drag.
    property bool magnetHeld: false
    readonly property var cfg: ({
        cellW: cellW, cellH: cellH, width: fieldWidth, height: fieldHeight,
        margin: 14, radius: 10, capture: 0.6, reach: 2.5,
        magnet: Settings.data.desktopMagnet && !magnetHeld
    })

    property var items: []
    property bool listed: false
    property bool stored: false
    readonly property bool ready: listed && stored && fieldWidth > 0 && fieldHeight > 0
    // name -> [x, y], as the user left them. Entries outlive their file and
    // are never rewritten to fit the screen, so a file that is saved again or
    // a monitor that drops out for a moment doesn't cost an icon its place.
    readonly property var positions: store.positions
    // The same, for the entries listed now and held inside this screen.
    readonly property var shown: {
        const out = {}
        if (!ready) return out
        const box = { cellW: cellW, cellH: cellH, width: fieldWidth, height: fieldHeight }
        for (const it of items) {
            const p = positions[it.name]
            if (!p) continue
            const c = Magnet.clamp({ x: p[0], y: p[1] }, box)
            out[it.name] = [c.x, c.y]
        }
        return out
    }

    property var selected: ({})                         // name -> true
    readonly property var selectedNames: items.map(i => i.name).filter(n => selected[n] === true)

    property bool dragging: false
    // A drag that left as a Wayland drag-and-drop, so the files can be
    // dropped on any window. Over the desktop it still moves the icons.
    property bool dndActive: false
    property string dndName: ""
    // An app is dragging files (it says so over IPC): the whole desktop takes
    // the drop, not just the icons.
    property bool appDrag: false
    // Quick Look is showing an icon (it says so over IPC): the selection
    // outlives the focus it took, and the preview follows the selection.
    property bool looking: false
    onLookingChanged: if (!looking) lookEnded.restart()
    onSelectedNamesChanged: if (looking && selectedNames.length) run(["look-update"].concat(selectedNames))
    property point dndGrab: Qt.point(0, 0)
    property string dir: ""
    property real dragDx: 0
    property real dragDy: 0
    property var guides: []

    property bool menuOpen: false
    property point menuAt: Qt.point(0, 0)
    property string renaming: ""
    property string renameError: ""
    property string renameAfterListing: ""
    readonly property bool modal: menuOpen || renaming !== ""

    onReadyChanged: place()

    function at(name) {
        const p = shown[name]
        return p ? { x: p[0], y: p[1] } : null
    }

    function othersOf(names) {
        const out = []
        for (const it of items) {
            if (names.includes(it.name)) continue
            const p = at(it.name)
            if (p) out.push(p)
        }
        return out
    }

    // Gives each new entry the next free slot from the top right. Entries
    // that already have a position keep it.
    function place() {
        if (!ready) return
        const fresh = items.filter(it => !positions[it.name])
        if (fresh.length === 0) return
        const next = Object.assign({}, positions)
        const listed = {}
        for (const it of items) listed[it.name] = true
        // Remembered places of files long gone don't pile up for ever.
        const gone = Object.keys(next).filter(n => !listed[n])
        if (gone.length > 200) for (const n of gone) delete next[n]
        const taken = othersOf([])
        for (const it of fresh) {
            const s = Magnet.freeSlot(taken, cfg)
            next[it.name] = [s.x, s.y]
            taken.push(s)
        }
        store.positions = next
    }

    function onListing(line) {
        let data
        try { data = JSON.parse(line) } catch (e) { return }
        // An unreadable folder (unmounted, mid-rename) keeps the last icons.
        if (data.error || !data.items) return
        items = data.items
        if (data.dir) dir = data.dir
        listed = true
        const names = items.map(i => i.name)
        const kept = {}
        for (const n of selectedNames) if (names.includes(n)) kept[n] = true
        selected = kept
        if (renaming !== "" && !names.includes(renaming)) renaming = ""
        place()
        if (renameAfterListing !== "" && names.includes(renameAfterListing)) {
            selected = ({ [renameAfterListing]: true })
            renaming = renameAfterListing
            renameAfterListing = ""
        }
    }

    function select(name, toggle) {
        if (toggle) {
            const next = Object.assign({}, selected)
            if (next[name]) delete next[name]
            else next[name] = true
            selected = next
        } else {
            selected = ({ [name]: true })
        }
    }

    // `dx, dy`: how far the pointer has moved since it grabbed `name`.
    function drag(name, dx, dy, free) {
        const from = at(name)
        if (!from) return
        magnetHeld = free
        const m = Magnet.magnetize({ x: from.x + dx, y: from.y + dy }, othersOf(selectedNames), cfg)
        dragging = true
        dragDx = m.x - from.x
        dragDy = m.y - from.y
        guides = m.guides
    }

    function drop(name, dx, dy) {
        const from = at(name)
        if (!dragging || !from) { cancelDrag(); return }
        const carried = selectedNames
        const taken = othersOf(carried)
        const next = Object.assign({}, positions)
        const landed = Magnet.settle({ x: from.x + dx, y: from.y + dy }, taken, cfg)
        const mx = landed.x - from.x, my = landed.y - from.y
        next[name] = [Math.round(landed.x), Math.round(landed.y)]
        taken.push(landed)
        for (const n of carried) {
            if (n === name) continue
            const p = at(n)
            const q = Magnet.resolve({ x: p.x + mx, y: p.y + my }, taken, cfg)
            next[n] = [Math.round(q.x), Math.round(q.y)]
            taken.push(q)
        }
        // Released first, so each icon eases from where it is shown to where
        // it lands instead of jumping there.
        cancelDrag()
        store.positions = next
    }

    function cancelDrag() {
        dragging = false
        dragDx = 0
        dragDy = 0
        guides = []
        magnetHeld = false
    }

    function uriOf(name) {
        return "file://" + (dir + "/" + name).split("/").map(encodeURIComponent).join("/")
    }

    // The pointer went past the drag threshold on `name`, grabbed at `grab`
    // (field coordinates): the selection becomes a drag-and-drop source.
    function beginDnd(name, grab) {
        if (dir === "") return false
        dndName = name
        dndGrab = grab
        dndActive = true
        return true
    }

    function endDnd() {
        dndActive = false
        dndName = ""
        cancelDrag()
    }

    function cleanUp() {
        if (!ready) return
        const list = items.map(it => Object.assign({ name: it.name }, at(it.name))).filter(i => i.x !== undefined)
        const out = Magnet.cleanUp(list, cfg)
        const next = Object.assign({}, positions)
        for (const n in out) next[n] = [out[n].x, out[n].y]
        store.positions = next
    }

    function run(args) { Quickshell.execDetached(scriptCmd.concat(args)) }
    function open(names) { if (names.length) run(["open"].concat(names)) }
    function look(names) { if (names.length) run(["look"].concat(names)) }
    function trash(names) { if (names.length) run(["trash"].concat(names)) }

    // Whatever is dropped on a desktop folder moves into it, from the
    // desktop itself or from any app. `paths` are absolute.
    function moveInto(folder, paths) {
        const list = paths.filter(q => q && q !== dir + "/" + folder)
        if (list.length === 0) return
        run(["move", folder].concat(list))
    }

    // Files dropped on the bare desktop move into the Desktop folder and land
    // where they were dropped: `x, y` is the pointer, in the icon field.
    function takeFiles(paths, x, y) {
        const list = paths.filter(q => q && q.slice(0, q.lastIndexOf("/")) !== dir)
        if (list.length === 0 || op.running) return
        op.then = res => {
            const names = res.names || []
            if (names.length === 0) return
            const taken = othersOf(names)
            const next = Object.assign({}, root.positions)
            const picked = {}
            let p = Magnet.settle({ x: x - cellW / 2, y: y - cellH / 2 }, taken, cfg)
            for (const n of names) {
                next[n] = [Math.round(p.x), Math.round(p.y)]
                picked[n] = true
                taken.push(p)
                p = Magnet.resolve({ x: p.x + cellW, y: p.y }, taken, cfg)
            }
            store.positions = next
            root.selected = picked
        }
        op.command = scriptCmd.concat(["take"]).concat(list)
        op.running = true
    }

    function pathsOf(urls) {
        const out = []
        for (const u of urls) {
            const s = u.toString()
            if (s.startsWith("file://")) out.push(decodeURIComponent(s.slice(7)))
        }
        return out
    }

    function newFolder(thenRename) {
        op.then = res => { if (res.ok && thenRename) root.renameAfterListing = res.name }
        op.command = scriptCmd.concat(["mkdir"])
        op.running = true
    }

    function commitRename(text) {
        const from = renaming, to = text.trim()
        if (from === "") return
        if (to === "" || to === from) { renaming = ""; renameError = ""; return }
        op.then = res => {
            if (!res.ok) { root.renameError = res.error || "No se pudo renombrar"; return }
            // The icon keeps its place under its new name.
            const next = Object.assign({}, root.positions)
            if (next[from]) { next[res.name] = next[from]; delete next[from] }
            store.positions = next
            root.selected = ({ [res.name]: true })
            root.renaming = ""
            root.renameError = ""
        }
        op.command = scriptCmd.concat(["rename", from, to])
        op.running = true
    }

    function openMenu(x, y) {
        menuAt = Qt.point(x, y)
        menuOpen = true
    }

    Process {
        id: watcher
        running: Settings.data.desktopIcons
        command: root.scriptCmd.concat(["watch"])
        stdout: SplitParser { onRead: line => root.onListing(line) }
        onExited: if (Settings.data.desktopIcons) rewatch.restart()
    }
    Timer { id: rewatch; interval: 3000; onTriggered: watcher.running = Settings.data.desktopIcons }

    Process {
        id: op
        property var then: null
        stdout: StdioCollector {
            onStreamFinished: {
                let res = { ok: false }
                try { res = JSON.parse(text) } catch (e) {}
                if (op.then) op.then(res)
                op.then = null
            }
        }
    }

    FileView {
        path: Settings.stateDir + "/desktop.json"
        atomicWrites: true
        printErrors: false
        onLoaded: root.stored = true
        onLoadFailed: root.stored = true
        onAdapterUpdated: writeAdapter()

        JsonAdapter {
            id: store
            property var positions: ({})
        }
    }

    IpcHandler {
        target: "desktop"

        function toggle(): void { Settings.data.desktopIcons = !Settings.data.desktopIcons }
        function toggleMagnet(): void { Settings.data.desktopMagnet = !Settings.data.desktopMagnet }
        function cleanUp(): void { root.cleanUp() }
        function newFolder(): void { root.newFolder(false) }
        function openFolder(): void { root.run(["open"]) }
        // A file manager calls this when a drag of its files starts and ends.
        function appDrag(on: bool): void { root.appDrag = on }
        // Quick Look calls this when its window opens and closes.
        function looking(on: bool): void { root.looking = on }
    }

    PanelWindow {
        id: win

        screen: root.primaryScreen
        visible: Settings.data.desktopIcons
        WlrLayershell.namespace: "wonderland-desktop"
        WlrLayershell.layer: WlrLayer.Bottom
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
        anchors { top: true; bottom: true; left: true; right: true }
        color: "transparent"
        mask: root.modal || root.dndActive || root.appDrag ? everything : iconsOnly

        Region { id: everything; item: iconField }
        Region { id: iconsOnly }

        function remask() {
            const regions = []
            for (let i = 0; i < icons.count; i++) {
                const it = icons.itemAt(i)
                if (it && it.placed) regions.push(it.region)
            }
            iconsOnly.regions = regions
        }

        Item {
            id: iconField
            anchors.fill: parent
            focus: true
            onWidthChanged: root.fieldWidth = width
            onHeightChanged: root.fieldHeight = height
            Component.onCompleted: { root.fieldWidth = width; root.fieldHeight = height }

            // Focus moving to a window or to the canvas ends the selection.
            readonly property bool active: Window.active
            onActiveChanged: if (!active && !root.modal && !root.dragging && !root.dndActive && !root.looking) root.selected = ({})
            // The preview closed: unless the focus came back here, the selection ends as it would have.
            Timer {
                id: lookEnded
                interval: 400
                onTriggered: if (!iconField.active && !root.looking && !root.modal && !root.dragging && !root.dndActive) root.selected = ({})
            }

            Keys.onPressed: event => {
                if (root.renaming !== "") return
                const names = root.selectedNames
                if (event.key === Qt.Key_Escape) {
                    root.menuOpen = false
                    root.selected = ({})
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.open(names)
                } else if (event.key === Qt.Key_Space) {
                    root.look(names)
                } else if (event.key === Qt.Key_Delete) {
                    root.trash(names)
                } else if (event.key === Qt.Key_F2 && names.length === 1) {
                    root.renaming = names[0]
                } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                    const all = {}
                    for (const it of root.items) all[it.name] = true
                    root.selected = all
                } else {
                    return
                }
                event.accepted = true
            }

            // Only reachable while a menu, a rename or an app's drag has the
            // whole surface. No button reaches us during a drag, so a press
            // with `appDrag` still set means its end was never announced.
            MouseArea {
                anchors.fill: parent
                enabled: root.modal || root.appDrag
                acceptedButtons: Qt.AllButtons
                onPressed: {
                    root.appDrag = false
                    root.menuOpen = false
                    if (root.renaming !== "") { root.renaming = ""; root.renameError = "" }
                }
            }

            // With the surface taking all input for the menu, the pointer
            // leaving it means it went to a window or the bar: let go.
            HoverHandler {
                id: hover
                onHoveredChanged: {
                    if (!hovered && root.menuOpen) leftMenu.restart()
                    else leftMenu.stop()
                }
            }
            Timer { id: leftMenu; interval: 700; onTriggered: root.menuOpen = false }

            // Our own drag coming back over the desktop: the icons follow it
            // and land where it is dropped, magnet included. Files dragged
            // from an app move to the Desktop folder (cut, not copy, as on a
            // folder icon) and land where they are dropped.
            DropArea {
                anchors.fill: parent
                enabled: root.dndActive || root.appDrag
                function ours(drag) { return drag.source && drag.source.desktopDrag === true }
                function files(drag) { return drag.hasUrls ? root.pathsOf(drag.urls) : [] }
                onEntered: drag => {
                    if (ours(drag)) return
                    if (files(drag).length === 0) { drag.accepted = false; return }
                    drag.accept(Qt.MoveAction)
                }
                onPositionChanged: drag => {
                    if (!ours(drag)) return
                    root.drag(root.dndName, drag.x - root.dndGrab.x, drag.y - root.dndGrab.y,
                              root.magnetHeld)
                }
                onExited: root.cancelDrag()
                onDropped: drop => {
                    if (ours(drop)) {
                        root.drop(root.dndName, drop.x - root.dndGrab.x, drop.y - root.dndGrab.y)
                        drop.accept(Qt.IgnoreAction)
                        return
                    }
                    const list = files(drop)
                    if (list.length === 0) { drop.accepted = false; return }
                    root.takeFiles(list, drop.x, drop.y)
                    drop.accept(Qt.MoveAction)
                }
            }

            Repeater {
                model: root.guides
                delegate: Rectangle {
                    required property var modelData
                    x: modelData.vertical ? Math.round(modelData.pos) : modelData.from
                    y: modelData.vertical ? modelData.from : Math.round(modelData.pos)
                    width: modelData.vertical ? 1 : modelData.to - modelData.from
                    height: modelData.vertical ? modelData.to - modelData.from : 1
                    color: Theme.p.accent
                    opacity: 0.2 + 0.8 * Math.max(0, Math.min(1, modelData.strength))
                }
            }

            Repeater {
                id: icons
                model: ScriptModel {
                    values: root.items
                    objectProp: "name"
                }
                delegate: DesktopIcon {
                    desk: root
                    field: iconField
                    onPlacedChanged: Qt.callLater(win.remask)
                }
                onItemAdded: Qt.callLater(win.remask)
                onItemRemoved: Qt.callLater(win.remask)
            }

            DesktopMenu {
                desk: root
                visible: root.menuOpen
                x: Math.max(4, Math.min(iconField.width - width - 4, root.menuAt.x))
                y: Math.max(4, Math.min(iconField.height - height - 4, root.menuAt.y))
                z: 10
            }
        }
    }
}
