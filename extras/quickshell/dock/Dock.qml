import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// A macOS-style dock at the bottom of each screen: Launchpad (the app grid;
// right click: Spotlight), pinned apps, then running apps that aren't pinned,
// then — past a separator — one tile per minimized window and the trash.
// Clicking a minimized tile, or an app whose windows are all minimized, brings
// the window back.
// A dot marks running apps (accent when focused), a launch bounces. Click: focus the app's window (flying the
// camera there; again cycles its windows; a suspended one relaunches) or
// launch it. Middle click: new window. Right click: its windows, New
// window, Keep in Dock, Quit. Dragging an app along the dock reorders the
// pinned ones (the others slide out of the way), dragging a running app in
// among them pins it there, and dragging a pinned one up and off the dock
// unpins it. Settings live in bar.json (dock* keys) and the settings panel.
Scope {
    id: root

    // Rabbit is the file manager; Dolphin only stands in where it isn't installed.
    // A dock that pinned it as Finder, its name before, keeps it.
    readonly property var defaultPins: ["hermes-desktop", "firefox", "wonderland-rabbit", "org.kde.dolphin",
        "org.kde.konsole", "Alacritty", "org.kde.kate", "org.kde.systemsettings"]
    // Desktop entries load asynchronously and byId() doesn't notify:
    // depending on the application list re-runs these once they're in.
    readonly property var entries: DesktopEntries.applications.values
    readonly property var pinnedIds: Settings.data.dockPinsCustomized
        ? (Settings.data.dockPinned || []).map(id => id === "wonderland-finder" ? "wonderland-rabbit" : id)
        : (entries.length, defaultPins.filter(id => DesktopEntries.byId(id)
            && !(id === "org.kde.dolphin" && DesktopEntries.byId("wonderland-rabbit"))))

    function setPins(ids) {
        Settings.data.dockPinned = ids
        Settings.data.dockPinsCustomized = true
    }
    function pin(key) { if (!pinnedIds.includes(key)) setPins(pinnedIds.concat([key])) }
    function unpin(key) { setPins(pinnedIds.filter(k => k !== key)) }
    // Puts an app at `pos` among the pinned ones on show, pinning it if it
    // wasn't. Pins of apps that aren't installed keep their place at the end.
    function movePin(key, pos) {
        const shown = apps.pinned.map(a => a.key).filter(k => k !== key)
        const hidden = pinnedIds.filter(id => id !== key && !shown.includes(id))
        shown.splice(Math.max(0, Math.min(pos, shown.length)), 0, key)
        setPins(shown.concat(hidden))
    }

    // Pinned apps in order, then running ones grouped by desktop entry
    // (heuristicLookup maps an app_id to its .desktop, StartupWMClass
    // included); a window without one groups by app_id.
    // heuristicLookup misses apps whose .desktop has no StartupWMClass and
    // whose app_id is reverse-DNS (com.nousresearch.hermes ↔ hermes-desktop):
    // fall back to the app_id's last segment against entry names and ids.
    function entryFor(appId) {
        const e = DesktopEntries.heuristicLookup(appId)
        if (e) return e
        const tail = String(appId || "").split(".").pop().toLowerCase()
        if (!tail) return null
        return entries.find(a => !a.noDisplay && (a.name.toLowerCase() === tail
            || a.id.toLowerCase() === tail || a.id.toLowerCase().startsWith(tail + "-"))) || null
    }

    readonly property var apps: {
        void entries.length
        const pinned = [], running = [], byKey = {}
        for (const id of pinnedIds) {
            const e = DesktopEntries.byId(id)
            if (!e || byKey[e.id]) continue
            const it = { kind: "app", key: e.id, entry: e, appId: "", pinned: true, windows: [] }
            byKey[e.id] = it
            pinned.push(it)
        }
        for (const w of Compositor.windows) {
            const e = entryFor(w.app_id)
            const key = e ? e.id : "app:" + w.app_id
            let it = byKey[key]
            if (!it) {
                it = { kind: "app", key: key, entry: e, appId: w.app_id, pinned: false, windows: [] }
                byKey[key] = it
                running.push(it)
            }
            it.windows.push(w)
        }
        return { pinned: pinned, running: running }
    }
    readonly property var model: [{ kind: "launchpad", key: "launchpad" }]
        .concat(apps.pinned)
        .concat(apps.running.length ? [{ kind: "sep", key: "sep-running" }] : [])
        .concat(apps.running)
        .concat([{ kind: "sep", key: "sep-trash" }])
        .concat(minimizedTiles)
        .concat([{ kind: "trash", key: "trash" }])

    // One tile per minimized window, oldest first, unless they go into their
    // app's icon instead (dockMinimizeIntoApp).
    readonly property var minimizedTiles: Settings.data.dockMinimizeIntoApp ? []
        : Compositor.windows.filter(w => w.minimized).map(w => ({
            kind: "minimized", key: "min-" + w.id, window: w, entry: entryFor(w.app_id), appId: w.app_id
        }))

    function title(it) {
        if (!it || it.kind === "sep") return ""
        if (it.kind === "launchpad") return "Launchpad"
        if (it.kind === "trash") return trashCount > 0 ? `Papelera (${trashCount})` : "Papelera"
        if (it.kind === "minimized") return it.window.title || (it.entry ? it.entry.name : it.appId)
        if (it.entry) return it.entry.name
        return it.windows && it.windows.length ? (it.windows[0].title || it.appId) : it.appId
    }
    function iconSource(it) {
        if (it.kind === "trash") return Quickshell.iconPath(trashCount > 0 ? "user-trash-full" : "user-trash", "user-trash")
        if (it.entry) return Quickshell.iconPath(it.entry.icon, "application-x-executable")
        return Quickshell.iconPath(it.appId, "application-x-executable")
    }

    // Click: the app's window — the next one if it already has focus — else
    // the last one minimized, else a relaunch if they're all suspended; no
    // window: launch. A minimized tile restores its window.
    function activate(it) {
        if (it.kind === "launchpad") { Popups.launchpadRequested(); return false }
        if (it.kind === "trash") { openTrash(); return false }
        if (it.kind === "minimized") { Compositor.restoreWindow(it.window.id); return false }
        const live = it.windows.filter(w => !w.suspended && !w.minimized)
        if (live.length) {
            const cur = live.findIndex(w => w.is_focused)
            Compositor.focusWindow(live[(cur + 1) % live.length].id)
            return false
        }
        const hidden = it.windows.filter(w => w.minimized)
        if (hidden.length) { Compositor.restoreWindow(hidden[hidden.length - 1].id); return false }
        if (it.windows.length) { Compositor.relaunch(it.windows[0].id); return true }
        if (it.entry) { it.entry.execute(); return true }
        return false
    }
    function quit(it) { for (const w of it.windows) Compositor.closeWindow(w.id) }
    function minimizeAll(it) { for (const w of it.windows.filter(w => !w.suspended && !w.minimized)) Compositor.minimizeWindow(w.id) }

    // ── trash ───────────────────────────────────────────────────────────
    property int trashCount: 0
    function openTrash() {
        // The default file manager opens it. Dolphin has a trash view of its own; the rest get the folder.
        Quickshell.execDetached(["sh", "-c", 'case "$(xdg-mime query default inode/directory 2>/dev/null)" in *dolphin*) exec dolphin trash:/ ;; esac; exec xdg-open "${XDG_DATA_HOME:-$HOME/.local/share}/Trash/files"'])
    }
    Process {
        id: trashProbe
        command: ["sh", "-c", 'ls -A "${XDG_DATA_HOME:-$HOME/.local/share}/Trash/files" 2>/dev/null | wc -l']
        stdout: StdioCollector { onStreamFinished: root.trashCount = parseInt(text) || 0 }
    }
    Timer { interval: 5000; running: Settings.data.dockEnabled; repeat: true; triggeredOnStart: true; onTriggered: trashProbe.running = true }

    readonly property var primaryScreen: Quickshell.screens.find(s => s.x === 0 && s.y === 0) || Quickshell.screens[0]

    // ── one dock per screen ─────────────────────────────────────────────
    Variants {
        model: Settings.data.dockEnabled ? (Settings.data.dockAllScreens ? Quickshell.screens : [root.primaryScreen]) : []

        delegate: PanelWindow {
            id: win
            required property var modelData
            screen: modelData

            readonly property real size: Settings.data.dockSize
            readonly property int pad: 7
            readonly property int gap: 6
            readonly property int bottomGap: 6
            readonly property real barHeight: size + 2 * pad
            readonly property bool autohide: Settings.data.dockAutohide
            readonly property bool hovered: dockHover.hovered || menu.visible
            readonly property bool shown: !autohide || hovered || peek.running || dragIndex >= 0

            // ── drag to reorder ───────────────────────────────────────
            // The model stays as it is while an icon is dragged (changing it
            // would rebuild every tile under the pointer): the dragged tile
            // follows the pointer and the ones between it and where it would
            // land slide one place over. The new order is saved on release.
            readonly property real step: size + gap
            readonly property int pinnedCount: root.apps.pinned.length
            property int dragIndex: -1      // row index of the dragged tile
            property int dragTo: -1         // row index it would land on
            property real dragDx: 0
            property bool dragRemove: false // lifted off the dock: unpin on release

            function dragMove(slot, dx, dy) {
                const pinned = slot.modelData.pinned
                const at = Math.round((slot.x + dx) / step)
                dragDx = dx
                dragRemove = pinned && dy < -size * 0.9
                if (dragRemove) dragTo = dragIndex
                else if (pinned) dragTo = Math.max(1, Math.min(at, pinnedCount))
                // A running app only lands once it's past the separator.
                else dragTo = at <= pinnedCount + 1 ? Math.max(1, at) : dragIndex
            }
            function dragEnd(it) {
                const from = dragIndex, to = dragTo, remove = dragRemove
                dragCancel()
                if (from < 0) return
                if (remove) root.unpin(it.key)
                else if (to !== from) root.movePin(it.key, to - 1)
            }
            function dragCancel() { dragIndex = -1; dragTo = -1; dragDx = 0; dragRemove = false }

            WlrLayershell.namespace: "wonderland-dock"
            WlrLayershell.layer: WlrLayer.Top
            anchors { bottom: true; left: true; right: true }
            color: "transparent"
            // Room above the bar for the hover label and the launch bounce.
            implicitHeight: Math.ceil(size + 2 * pad + bottomGap + 40)
            exclusionMode: autohide ? ExclusionMode.Ignore : ExclusionMode.Normal
            exclusiveZone: autohide ? 0 : barHeight + bottomGap
            // Clicks pass through everywhere but the dock.
            mask: Region { item: hitArea }

            Item {
                id: hitArea
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: bg.width + 2 * win.gap
                height: win.shown ? win.barHeight + win.bottomGap : 3
                // Inside the hit area: the window spans the whole screen width.
                HoverHandler { id: dockHover }
            }

            // Show briefly when autohidden and something launches.
            Timer { id: peek; interval: 1400 }

            Item {
                id: body
                anchors.fill: parent
                transform: Translate {
                    y: win.shown ? 0 : win.barHeight + win.bottomGap + 4
                    Behavior on y { NumberAnimation { duration: Theme.animMs * 1.4; easing.type: Easing.OutCubic } }
                }

                // Soft shadow + glass card, like the Hermes popovers.
                Rectangle {
                    anchors.fill: bg
                    anchors.margins: -1
                    anchors.topMargin: 2
                    radius: bg.radius + 1
                    color: Theme.p.shadow
                    opacity: Theme.dark ? 0.45 : 0.10
                }
                Rectangle {
                    id: bg
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: win.bottomGap
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: row.width + 2 * win.pad + 4
                    height: win.barHeight
                    radius: 18
                    readonly property color base: Theme.p.elevated
                    color: Theme.glassy(base, Theme.dark ? 0.86 : 0.88)
                    border.width: 1
                    border.color: Theme.p.stroke2
                }

                Row {
                    id: row
                    anchors.bottom: bg.bottom
                    anchors.bottomMargin: win.pad
                    anchors.horizontalCenter: bg.horizontalCenter
                    spacing: win.gap
                    height: win.size

                    Repeater {
                        model: root.model
                        delegate: Item {
                            id: slot
                            required property var modelData
                            required property int index
                            readonly property bool isSep: modelData.kind === "sep"
                            readonly property bool running: modelData.kind === "app" && modelData.windows.length > 0
                            readonly property bool focused: running && modelData.windows.some(w => w.is_focused)
                            readonly property bool asleep: running && modelData.windows.every(w => w.suspended)
                            property real lift: 0
                            // Apps with a desktop entry can be pinned, so they can be dragged.
                            readonly property bool draggable: modelData.kind === "app" && !!modelData.entry
                            readonly property bool dragged: win.dragIndex === index
                            readonly property real shiftX: dragged ? win.dragDx
                                : win.dragIndex < 0 ? 0
                                : win.dragIndex < index && index <= win.dragTo ? -win.step
                                : win.dragTo <= index && index < win.dragIndex ? win.step : 0

                            z: dragged ? 10 : 0
                            transform: Translate {
                                x: slot.shiftX
                                Behavior on x { enabled: !slot.dragged; NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                            }
                            // Compositor updates rebuild the tiles: a drag can't outlive its tile.
                            Component.onDestruction: if (dragged) win.dragCancel()

                            width: isSep ? 13 : win.size
                            height: win.size
                            anchors.bottom: parent.bottom

                            function bounce() { win.autohide && peek.restart(); bouncer.restart() }
                            SequentialAnimation {
                                id: bouncer
                                loops: 2
                                NumberAnimation { target: slot; property: "lift"; to: win.size * 0.45; duration: 260; easing.type: Easing.OutQuad }
                                NumberAnimation { target: slot; property: "lift"; to: 0; duration: 300; easing.type: Easing.OutBounce }
                            }

                            Rectangle {
                                visible: slot.isSep
                                anchors.centerIn: parent
                                width: 1
                                height: win.size * 0.72
                                color: Theme.p.stroke2
                            }

                            // Launchpad: an accent tile, like the system icon.
                            Rectangle {
                                visible: slot.modelData.kind === "launchpad"
                                anchors.fill: icon
                                anchors.margins: parent.width * 0.06
                                radius: width * 0.24
                                gradient: Gradient {
                                    GradientStop { position: 0; color: Theme.p.accentHover }
                                    GradientStop { position: 1; color: Theme.p.accent }
                                }
                                Icon { anchors.centerIn: parent; name: "layout-grid"; size: parent.width * 0.5; color: Theme.p.onAccent }
                            }

                            // Minimized window: a little window — card with a title
                            // strip — wearing its app's icon.
                            Rectangle {
                                id: card
                                visible: slot.modelData.kind === "minimized"
                                anchors.centerIn: parent
                                width: parent.width * 0.9
                                height: parent.height * 0.74
                                radius: 6
                                color: Theme.p.elevated
                                border.width: 1
                                border.color: Theme.p.stroke2
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 1
                                    height: parent.height * 0.18
                                    radius: 5
                                    color: Theme.p.stroke3
                                }
                                Image {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.verticalCenterOffset: parent.height * 0.08
                                    width: parent.height * 0.6
                                    height: width
                                    source: card.visible ? root.iconSource(slot.modelData) : ""
                                    sourceSize: Qt.size(win.size * 2, win.size * 2)
                                    smooth: true
                                    mipmap: true
                                }
                            }

                            Image {
                                id: icon
                                width: parent.width
                                height: width
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: slot.lift + (slot.dragged ? 6 : 0)
                                visible: !slot.isSep && slot.modelData.kind !== "launchpad" && slot.modelData.kind !== "minimized"
                                source: visible ? root.iconSource(slot.modelData) : ""
                                sourceSize: Qt.size(win.size * 2, win.size * 2)
                                smooth: true
                                mipmap: true
                                opacity: slot.dragged && win.dragRemove ? 0.35 : slot.asleep ? 0.55 : 1
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }

                            Rectangle {
                                visible: slot.running
                                width: 4
                                height: 4
                                radius: 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: 1
                                color: slot.focused ? Theme.p.accent : slot.asleep ? Theme.p.text4 : Theme.p.text2
                            }

                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                enabled: !slot.isSep
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                                cursorShape: slot.dragged ? Qt.ClosedHandCursor : Qt.ArrowCursor
                                // Row coordinates: the tile moves under the pointer, the row doesn't.
                                property point pressAt
                                property bool wasDrag: false
                                onPressed: event => {
                                    wasDrag = false
                                    pressAt = mapToItem(row, event.x, event.y)
                                }
                                onPositionChanged: event => {
                                    if (!slot.draggable || !(event.buttons & Qt.LeftButton)) return
                                    const p = mapToItem(row, event.x, event.y)
                                    const dx = p.x - pressAt.x, dy = p.y - pressAt.y
                                    if (!slot.dragged) {
                                        // A click that wobbles a few pixels is still a click.
                                        if (Math.abs(dx) < 8 && Math.abs(dy) < 8) return
                                        wasDrag = true
                                        win.dragIndex = slot.index
                                    }
                                    win.dragMove(slot, dx, dy)
                                }
                                onReleased: if (slot.dragged) win.dragEnd(slot.modelData)
                                onCanceled: if (slot.dragged) win.dragCancel()
                                onClicked: event => {
                                    if (wasDrag) { wasDrag = false; return }
                                    const it = slot.modelData
                                    if (event.button === Qt.RightButton) {
                                        if (it.kind === "launchpad") Popups.spotlightRequested()
                                        else menu.openFor(it, slot)
                                    } else if (event.button === Qt.MiddleButton) {
                                        if (it.entry) { it.entry.execute(); slot.bounce() }
                                    } else if (root.activate(it)) {
                                        slot.bounce()
                                    }
                                }
                            }

                            // Name above the icon while hovered (macOS label).
                            Rectangle {
                                visible: !slot.isSep && !menu.visible
                                    && (win.dragIndex < 0 ? mouse.containsMouse : slot.dragged && win.dragRemove)
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.top
                                anchors.bottomMargin: 10
                                width: label.implicitWidth + 18
                                height: 24
                                radius: 7
                                color: Theme.p.elevated
                                border.width: 1
                                border.color: Theme.p.stroke2
                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: slot.isSep ? "" : slot.dragged && win.dragRemove ? "Quitar del Dock" : root.title(slot.modelData)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: Theme.p.text
                                }
                            }
                        }
                    }
                }
            }

            // ── context menu ──────────────────────────────────────────
            PopupWindow {
                id: menu
                property var item: null
                property Item slotItem: null
                function openFor(it, s) {
                    item = it
                    slotItem = s
                    anchor.item = s
                    visible = true
                }
                anchor.edges: Edges.Top
                anchor.gravity: Edges.Top
                anchor.margins.bottom: 12
                grabFocus: true
                color: "transparent"
                implicitWidth: 240
                implicitHeight: menuCol.implicitHeight + 12

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: Theme.p.elevated
                    border.width: 1
                    border.color: Theme.p.stroke2

                    Column {
                        id: menuCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        spacing: 1

                        component MenuEntry: Rectangle {
                            id: entry
                            property string text
                            property bool header: false
                            property bool danger: false
                            signal triggered()
                            width: parent ? parent.width : 0
                            height: header ? 26 : 28
                            radius: 6
                            color: !header && entryMouse.containsMouse ? Theme.p.accent : "transparent"
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: entry.text
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: entry.header ? 11 : 13
                                font.weight: entry.header ? Font.DemiBold : Font.Normal
                                color: entry.header ? Theme.p.text3
                                    : entryMouse.containsMouse ? Theme.p.onAccent
                                    : entry.danger ? Theme.p.red : Theme.p.text
                            }
                            MouseArea {
                                id: entryMouse
                                anchors.fill: parent
                                enabled: !entry.header
                                hoverEnabled: true
                                onClicked: { menu.visible = false; entry.triggered() }
                            }
                        }
                        component MenuSep: Rectangle { width: parent ? parent.width : 0; height: 9; color: "transparent"; Rectangle { anchors.centerIn: parent; width: parent.width - 12; height: 1; color: Theme.p.stroke3 } }

                        // Windows of the app.
                        MenuEntry {
                            header: true
                            visible: !!menu.item && menu.item.kind === "app" && menu.item.windows.length > 0
                            text: "Ventanas"
                        }
                        Repeater {
                            model: menu.item && menu.item.kind === "app" ? menu.item.windows : []
                            delegate: MenuEntry {
                                required property var modelData
                                text: (modelData.is_focused ? "✓ " : "") + (modelData.title || modelData.app_id)
                                    + (modelData.suspended ? "  (suspendida)" : modelData.minimized ? "  (minimizada)" : "")
                                onTriggered: modelData.suspended ? Compositor.relaunch(modelData.id) : Compositor.focusWindow(modelData.id)
                            }
                        }
                        MenuSep { visible: !!menu.item && menu.item.kind === "app" && menu.item.windows.length > 0 }

                        MenuEntry {
                            visible: !!menu.item && !!menu.item.entry
                            text: menu.item && menu.item.windows && menu.item.windows.length ? "Nueva ventana" : "Abrir"
                            onTriggered: { menu.item.entry.execute(); if (menu.slotItem) menu.slotItem.bounce() }
                        }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "app" && !!menu.item.entry
                            text: menu.item && menu.item.pinned ? "Quitar del Dock" : "Mantener en el Dock"
                            onTriggered: menu.item.pinned ? root.unpin(menu.item.key) : root.pin(menu.item.key)
                        }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "app"
                                && menu.item.windows.some(w => !w.suspended && !w.minimized)
                            text: "Minimizar"
                            onTriggered: root.minimizeAll(menu.item)
                        }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "trash"
                            text: "Abrir la Papelera"
                            onTriggered: root.openTrash()
                        }
                        // A minimized window's own tile.
                        MenuEntry {
                            header: true
                            visible: !!menu.item && menu.item.kind === "minimized"
                            text: menu.item && menu.item.kind === "minimized" ? root.title(menu.item) : ""
                        }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "minimized"
                            text: "Restaurar"
                            onTriggered: Compositor.restoreWindow(menu.item.window.id)
                        }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "minimized"
                            danger: true
                            text: "Cerrar"
                            onTriggered: Compositor.closeWindow(menu.item.window.id)
                        }
                        MenuSep { visible: !!menu.item && menu.item.kind === "app" && menu.item.windows.length > 0 }
                        MenuEntry {
                            visible: !!menu.item && menu.item.kind === "app" && menu.item.windows.length > 0
                            danger: true
                            text: menu.item && menu.item.windows && menu.item.windows.length > 1 ? `Cerrar ${menu.item.windows.length} ventanas` : "Cerrar"
                            onTriggered: root.quit(menu.item)
                        }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "dock"
        function toggle(): void { Settings.data.dockEnabled = !Settings.data.dockEnabled }
        function toggleAutohide(): void { Settings.data.dockAutohide = !Settings.data.dockAutohide }
    }
}
