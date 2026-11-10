import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// macOS-style Launchpad: every installed app as a grid of icons over the
// whole active monitor, in pages. Type to filter, arrows move, Enter opens,
// the wheel / PageUp / PageDown / the dots change page, Esc or a click on the
// background closes. Opened from the Dock's first tile, or
// `qs ipc call launchpad toggle`.
//
// Its own layer surface with exclusive keyboard focus, like Spotlight. With
// a `wonderland-launchpad` blur window rule the compositor frosts the desktop
// behind it.
Scope {
    id: root

    property bool open: false
    property var screen: null
    property string query: ""
    property int page: 0
    property int sel: -1           // index into `shown`; -1: nothing selected

    readonly property int cols: 7
    readonly property int rows: (screen ? screen.height : 1080) >= 900 ? 5 : 4
    readonly property int perPage: cols * rows

    // Desktop entries load asynchronously: depend on the list itself.
    readonly property var allApps: {
        const list = DesktopEntries.applications.values.filter(a => !a.noDisplay)
        list.sort((a, b) => fold(a.name).localeCompare(fold(b.name)))
        return list
    }
    // Filtered by the query: names starting with it first, then names with a
    // word starting with it, then anything mentioning it.
    readonly property var shown: {
        const q = fold(query.trim())
        if (!q) return allApps
        const ranked = []
        for (const a of allApps) {
            const name = fold(a.name)
            let s = 0
            if (name.startsWith(q)) s = 3
            else if (name.split(/[\s\-_.]+/).some(w => w.startsWith(q))) s = 2
            else if (name.includes(q) || fold(a.genericName).includes(q)
                     || fold(String(a.keywords || "")).includes(q)) s = 1
            if (s) ranked.push({ s: s, a: a })
        }
        ranked.sort((x, y) => y.s - x.s)   // stable: stays alphabetical within a rank
        return ranked.map(r => r.a)
    }
    readonly property int pageCount: Math.max(1, Math.ceil(shown.length / perPage))

    // The text colour at alpha `a`: highlights that read on any tint.
    function veil(a) {
        const c = Qt.darker(Theme.p.text, 1.0)   // palette entries are strings
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    function fold(s) {
        return String(s || "").toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "")
    }

    function toggle() { if (open) close(); else show() }
    function show() {
        screen = activeScreen()
        setQuery("")
        open = true
    }
    function close() { open = false }

    function activeScreen() {
        const outs = Compositor.state ? Compositor.state.outputs || [] : []
        const active = outs.find(o => o.active)
        if (active) {
            const s = Quickshell.screens.find(s => s.name === active.name)
            if (s) return s
        }
        return Quickshell.screens[0]
    }

    function setQuery(q) {
        query = q
        sel = q.trim() ? 0 : -1
        setPage(0)
    }
    function setPage(n) {
        page = Math.max(0, Math.min(pageCount - 1, n))
        pager.currentIndex = page
    }
    function turn(d) {
        const before = page
        setPage(page + d)
        if (page !== before && sel >= 0) sel = Math.min(shown.length - 1, page * perPage)
    }
    function select(i) {
        if (!shown.length) return
        sel = Math.max(0, Math.min(shown.length - 1, i))
        setPage(Math.floor(sel / perPage))
    }
    // Arrows: across pages sideways, inside the page up and down.
    function move(dx, dy) {
        if (!shown.length) return
        if (sel < 0) { select(page * perPage); return }
        if (dx) { select(sel + dx); return }
        const next = sel + dy * cols
        if (next < 0 || Math.floor(next / perPage) !== Math.floor(sel / perPage)) return
        select(Math.min(next, shown.length - 1))
    }
    function launch(i) {
        const a = shown[i]
        if (!a) return
        close()
        a.execute()
    }

    Connections {
        target: Popups
        function onLaunchpadRequested() { root.toggle() }
    }

    PanelWindow {
        id: win
        screen: root.screen
        visible: root.open
        WlrLayershell.namespace: "wonderland-launchpad"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore
        anchors { top: true; bottom: true; left: true; right: true }
        // Glass: a tint the compositor frosts. Without it, a near-solid veil,
        // or the desktop would show through sharp.
        color: {
            const c = Qt.darker(Theme.p.chrome, 1.0)
            return Qt.rgba(c.r, c.g, c.b, Theme.glass === true ? 0.62 : 0.94)
        }

        onVisibleChanged: if (visible) { field.forceActiveFocus(); intro.restart() }

        // A click on the background closes, as on macOS.
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
            onWheel: wheel => {
                if (wheelCooldown.running) return
                const d = Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y) ? wheel.angleDelta.x : wheel.angleDelta.y
                if (!d) return
                root.turn(d < 0 ? 1 : -1)
                wheelCooldown.restart()
            }
        }
        Timer { id: wheelCooldown; interval: 260 }

        Item {
            id: content
            anchors.fill: parent

            ParallelAnimation {
                id: intro
                NumberAnimation { target: content; property: "opacity"; from: 0; to: 1; duration: Theme.animMs * 1.3 }
                NumberAnimation { target: content; property: "scale"; from: 1.08; to: 1; duration: Theme.animMs * 1.6; easing.type: Easing.OutCubic }
            }

            // ── search ──────────────────────────────────────────────────
            Rectangle {
                id: search
                anchors.top: parent.top
                anchors.topMargin: Math.round(parent.height * 0.055)
                anchors.horizontalCenter: parent.horizontalCenter
                width: 280
                height: 34
                radius: 9
                color: root.veil(Theme.dark ? 0.10 : 0.07)
                border.width: 1
                border.color: field.activeFocus ? root.veil(0.28) : root.veil(0.14)

                MouseArea { anchors.fill: parent; onClicked: field.forceActiveFocus() }

                Icon {
                    id: lens
                    name: "search"
                    size: 15
                    color: Theme.p.text3
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextInput {
                    id: field
                    anchors.left: lens.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    color: Theme.p.text
                    selectionColor: Theme.p.accentSoft
                    selectedTextColor: Theme.p.text
                    clip: true
                    focus: true
                    text: root.query
                    onTextEdited: root.setQuery(text)

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: !field.text
                        text: I18n.tr("Buscar")
                        font: field.font
                        color: Theme.p.text3
                    }

                    Keys.onPressed: event => {
                        let used = true
                        if (event.key === Qt.Key_Escape) { if (root.query) root.setQuery(""); else root.close() }
                        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Tab) root.move(1, 0)
                        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Backtab) root.move(-1, 0)
                        else if (event.key === Qt.Key_Down) root.move(0, 1)
                        else if (event.key === Qt.Key_Up) root.move(0, -1)
                        else if (event.key === Qt.Key_PageDown) root.turn(1)
                        else if (event.key === Qt.Key_PageUp) root.turn(-1)
                        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { if (root.sel >= 0) root.launch(root.sel) }
                        else used = false
                        event.accepted = used
                    }
                }
            }

            // ── pages ───────────────────────────────────────────────────
            ListView {
                id: pager
                anchors.top: search.bottom
                anchors.topMargin: Math.round(parent.height * 0.035)
                anchors.bottom: dots.top
                anchors.bottomMargin: 10
                // The whole screen wide: a page slides in from the screen's
                // edge, so no icon is cut at the grid's border on the way.
                anchors.left: parent.left
                anchors.right: parent.right
                readonly property real gridWidth: Math.min(width * 0.84, 1500)
                orientation: ListView.Horizontal
                snapMode: ListView.SnapOneItem
                highlightRangeMode: ListView.StrictlyEnforceRange
                preferredHighlightBegin: 0
                preferredHighlightEnd: 0
                highlightMoveDuration: Settings.data.reduceMotion ? 0 : 320
                boundsBehavior: Flickable.DragOverBounds
                clip: true
                model: root.pageCount
                onCurrentIndexChanged: if (currentIndex >= 0) root.page = currentIndex

                delegate: Item {
                    id: pageItem
                    required property int index
                    width: pager.width
                    height: pager.height
                    readonly property real cellW: pager.gridWidth / root.cols
                    readonly property real gridX: (width - pager.gridWidth) / 2
                    readonly property real cellH: height / root.rows
                    readonly property real iconSize: Math.max(48, Math.min(112, cellH - 62, cellW - 40))

                    // The flickable takes the press: close from in here too.
                    MouseArea { anchors.fill: parent; onClicked: root.close() }

                    Repeater {
                        model: root.shown.slice(pageItem.index * root.perPage, (pageItem.index + 1) * root.perPage)
                        delegate: Item {
                            id: cell
                            required property var modelData
                            required property int index
                            readonly property int global: pageItem.index * root.perPage + index
                            readonly property bool selected: root.sel === global
                            x: pageItem.gridX + (index % root.cols) * pageItem.cellW
                            y: Math.floor(index / root.cols) * pageItem.cellH
                            width: pageItem.cellW
                            height: pageItem.cellH

                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 12, pageItem.iconSize + 76)
                                height: Math.min(parent.height - 6, pageItem.iconSize + 52)
                                radius: 16
                                color: root.veil(cell.selected ? 0.16 : cellMouse.containsMouse ? 0.08 : 0)
                                border.width: cell.selected ? 1 : 0
                                border.color: root.veil(0.22)
                            }

                            Image {
                                id: appIcon
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.verticalCenterOffset: -14
                                width: pageItem.iconSize
                                height: width
                                source: Quickshell.iconPath(cell.modelData.icon, "application-x-executable")
                                sourceSize: Qt.size(224, 224)
                                asynchronous: true
                                smooth: true
                                mipmap: true
                                scale: cellMouse.pressed ? 0.92 : 1
                                Behavior on scale { NumberAnimation { duration: 90 } }
                            }
                            Text {
                                anchors.top: appIcon.bottom
                                anchors.topMargin: 7
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(parent.width - 20, pageItem.iconSize + 64)
                                horizontalAlignment: Text.AlignHCenter
                                text: cell.modelData.name
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: Theme.p.text
                                // Reads over whatever the frost lets through.
                                style: Text.Raised
                                styleColor: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.6)
                            }

                            MouseArea {
                                id: cellMouse
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 12, pageItem.iconSize + 76)
                                height: Math.min(parent.height - 6, pageItem.iconSize + 52)
                                hoverEnabled: true
                                onClicked: root.launch(cell.global)
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.shown.length === 0
                    text: root.query.trim() ? I18n.tr("Sin resultados para «%1»").arg(root.query.trim()) : I18n.tr("No hay aplicaciones")
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    color: Theme.p.text3
                }
            }

            // ── page dots ───────────────────────────────────────────────
            Row {
                id: dots
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Math.max(48, Math.round(parent.height * 0.05))
                anchors.horizontalCenter: parent.horizontalCenter
                height: 22
                spacing: 4
                opacity: root.pageCount > 1 ? 1 : 0

                Repeater {
                    model: root.pageCount
                    delegate: Item {
                        required property int index
                        width: 18
                        height: 22
                        Rectangle {
                            anchors.centerIn: parent
                            width: 8
                            height: 8
                            radius: 4
                            color: root.veil(parent.index === root.page ? 0.9 : 0.3)
                            Behavior on color { ColorAnimation { duration: Theme.animMs } }
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.setPage(parent.index) }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "launchpad"
        function toggle(): void { root.toggle() }
        function open(): void { root.show() }
        function close(): void { root.close() }
        function search(text: string): void { root.setQuery(text) }
        function goToPage(n: int): void { root.setPage(n) }
        function move(dx: int, dy: int): void { root.move(dx, dy) }
        function launchSelected(): void { if (root.sel >= 0) root.launch(root.sel) }
        // "<open|closed> <apps shown> <pages> <page> <selected app or ->"
        function status(): string {
            return `${root.open ? "open" : "closed"} ${root.shown.length} ${root.pageCount} ${root.page} ${root.sel >= 0 && root.shown[root.sel] ? root.shown[root.sel].id : "-"}`
        }
    }
}
