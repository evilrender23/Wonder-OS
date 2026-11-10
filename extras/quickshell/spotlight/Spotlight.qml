import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// Spotlight-style search, centered on the active monitor: one field, results
// grouped under a top hit as you type — apps, canvas windows (suspended ones
// relaunch), bookmarks, system actions, a calculator and a web fallback.
// ↑/↓ (or Tab, Ctrl+N/P) move, Enter opens, Esc or a click outside closes.
//
// Its own layer surface with exclusive keyboard focus: a bar popup opened
// from a keybinding has no input serial to grab with, so it never got keys.
Scope {
    id: root

    property bool open: false
    property var screen: null
    property string query: ""
    property var refs: []          // activation payloads, by ListModel `ref`
    property int listHeight: 0     // exact: rows + section headers

    function toggle() { if (open) close(); else show() }
    function show() {
        screen = activeScreen()
        query = ""
        rebuild()
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

    Connections {
        target: Popups
        function onSpotlightRequested() { root.toggle() }
        function onPanelRequested(id) { if (id === "launcher" || id === "spotlight") root.toggle() }
    }

    // ── matching ────────────────────────────────────────────────────────
    function fold(s) {
        return String(s || "").toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "")
    }
    function score(q, s, fuzzy) {
        // Prefix > word start > substring > subsequence (contiguous runs
        // win). Subsequences only for names: across long keyword lists
        // almost any query is a subsequence of something.
        s = fold(s)
        if (!q || !s) return 0
        if (s.startsWith(q)) return 1000 - s.length
        let word = -1
        for (let k = s.indexOf(q, 1); k > 0; k = s.indexOf(q, k + 1))
            if (" -_.;/".includes(s[k - 1])) { word = k; break }
        if (word >= 0) return 800 - word
        const direct = s.indexOf(q)
        if (direct >= 0) return 600 - direct
        if (fuzzy === false || q.length < 2) return 0
        let i = 0, last = -2, sc = 0
        for (const ch of q) {
            const j = s.indexOf(ch, i)
            if (j < 0) return 0
            sc += j === last + 1 ? 12 : 2
            last = j
            i = j + 1
        }
        // Mostly-contiguous only: "fx" finds Firefox, not every long name.
        return sc >= q.length * 6 ? Math.min(sc, 400) : 0
    }

    readonly property var systemActions: [
        { title: "AquilesAI", detail: I18n.tr("el agente del escritorio"), icon: "sparkles",
          keys: "aquiles aquilesai ia ai agente agent chat hermes claude codex antigravity asistente", run: () => Popups.aquilesRequested() },
        { title: I18n.tr("Ajustes de pantallas"), detail: I18n.tr("resolución, frecuencia, disposición"), icon: "monitor",
          keys: "pantallas monitores displays resolucion frecuencia hz escala", run: () => Popups.displaysRequested() },
        { title: I18n.tr("Fondos de pantalla"), detail: I18n.tr("fondos animados, tus imágenes y sus ajustes"), icon: "image",
          keys: "wallpapers wallpaper fondos fondo de pantalla escritorio background imagen foto importar", run: () => Popups.wallpapersRequested() },
        { title: I18n.tr("Captura de pantalla"), detail: I18n.tr("ventana, área o pantalla completa"), icon: "camera",
          keys: "captura capturar screenshot pantallazo imagen recorte", run: () => Capture.show("last") },
        { title: I18n.tr("Grabar la pantalla"), detail: Capture.recording ? I18n.tr("hay una grabación en curso: la detiene") : I18n.tr("vídeo de una ventana, un área o una pantalla"), icon: "video",
          keys: "grabar grabacion recorder record video screencast", run: () => Capture.toggleRecord() },
        { title: I18n.tr("Alinear pantallas"), detail: I18n.tr("pon los otros monitores junto a este"), icon: "layout-grid",
          keys: "alinear align outputs monitores", run: () => Compositor.action("align-outputs") },
        { title: I18n.tr("Ver todas las ventanas"), detail: I18n.tr("zoom para abarcar el lienzo"), icon: "zoom-out",
          keys: "zoom fit todas ventanas overview", run: () => Compositor.action("zoom-to-fit") },
        { title: Theme.dark ? I18n.tr("Tema claro") : I18n.tr("Tema oscuro"), detail: I18n.tr("apariencia de la barra"), icon: Theme.dark ? "sun" : "moon",
          keys: "tema theme oscuro claro dark light modo", run: () => Settings.data.themeMode = Theme.dark ? "light" : "dark" },
        { title: "Terminal", detail: I18n.tr("nueva terminal"), icon: "terminal",
          keys: "terminal consola shell", run: () => Session.terminal() },
        { title: I18n.tr("Bloquear pantalla"), detail: "", icon: "lock", keys: "bloquear lock", run: () => Session.lock() },
        { title: I18n.tr("Suspender"), detail: "", icon: "moon-star", keys: "suspender dormir sleep suspend", run: () => Session.suspend() },
        { title: I18n.tr("Cerrar sesión"), detail: I18n.tr("salir de WonderlandWM"), icon: "log-out", keys: "cerrar sesion logout salir", run: () => Session.logout() },
        { title: I18n.tr("Reiniciar"), detail: "", icon: "rotate-cw", keys: "reiniciar reboot restart", run: () => Session.reboot() },
        { title: I18n.tr("Apagar"), detail: "", icon: "power", keys: "apagar shutdown poweroff", run: () => Session.poweroff() }
    ]

    function calc(q) {
        const t = q.trim()
        if (!/^[0-9+\-*/().,%^\s]+$/.test(t) || !/\d/.test(t) || !/[+\-*/%^]/.test(t)) return null
        try {
            const v = Function(`"use strict"; return (${t.replace(/,/g, ".").replace(/\^/g, "**")})`)()
            if (typeof v !== "number" || !isFinite(v)) return null
            return String(parseFloat(v.toPrecision(12)))
        } catch (e) {
            return null
        }
    }

    function rebuild() {
        results.clear()
        const q = fold(query.trim())
        const refs = []
        if (!q) { root.refs = refs; root.listHeight = 0; list.currentIndex = -1; return }

        const groups = { apps: [], windows: [], marks: [], system: [], walls: [] }
        for (const a of DesktopEntries.applications.values) {
            if (a.noDisplay) continue
            const s = Math.max(score(q, a.name), score(q, a.genericName, false) * 0.7,
                               score(q, String(a.keywords || ""), false) * 0.6, score(q, a.id, false) * 0.5)
            if (s > 0) groups.apps.push({ s: s, row: { kind: "app", title: a.name,
                detail: a.genericName || a.comment || "", iconSource: Quickshell.iconPath(a.icon, "application-x-executable"),
                icon: "", hint: I18n.tr("Abrir") }, run: () => a.execute() })
        }
        for (const w of Compositor.windows) {
            const s = Math.max(score(q, w.title), score(q, w.app_id)) * 0.95
            if (s > 0) {
                const app = DesktopEntries.heuristicLookup(w.app_id)
                groups.windows.push({ s: s, row: { kind: "window", title: w.title || w.app_id,
                    detail: w.suspended ? I18n.tr("suspendida") + " · " + w.app_id : w.app_id,
                    iconSource: app ? Quickshell.iconPath(app.icon, true) : "", icon: w.suspended ? "moon" : "app-window",
                    hint: w.suspended ? I18n.tr("Relanzar") : I18n.tr("Ir") },
                    run: () => w.suspended ? Compositor.relaunch(w.id) : Compositor.focusWindow(w.id) })
            }
        }
        for (const b of Compositor.bookmarkNames) {
            const s = score(q, b) * 0.9 + (score(q, "marcador bookmark") > 0 ? 100 : 0)
            if (s > 0) groups.marks.push({ s: s, row: { kind: "bookmark", title: b, detail: I18n.tr("marcador"),
                iconSource: "", icon: "bookmark", hint: I18n.tr("Ir") }, run: () => Compositor.goToBookmark(b) })
        }
        for (const act of systemActions) {
            const s = Math.max(score(q, act.title), score(q, act.keys, false) * 0.8) * 0.9
            if (s > 0) groups.system.push({ s: s, row: { kind: "system", title: act.title, detail: act.detail,
                iconSource: "", icon: act.icon, hint: I18n.tr("Ejecutar") }, run: act.run })
        }
        // «wallpapers» / «fondos» lists them all; a name («aurora») finds one.
        const wallWord = q.length >= 3 && Math.max(score(q, "wallpapers"), score(q, "fondos de pantalla"), score(q, "fondo"), score(q, "background")) > 0
        for (const w of Wallpapers.list) {
            const named = Math.max(score(q, w.name), score(q, w.component, false) * 0.8, score(q, (w.tags || []).join(" "), false) * 0.5)
            const s = wallWord ? 300 + named * 0.1 + (Wallpapers.current === w.id ? 50 : 0) : named * 0.7
            if (s > 0) groups.walls.push({ s: s, row: { kind: "wallpaper", title: w.name,
                detail: Wallpapers.current === w.id ? I18n.tr("fondo actual") : I18n.tr("fondo de pantalla"),
                iconSource: w.preview ? "file://" + w.preview : "", icon: "image", hint: I18n.tr("Aplicar") },
                run: () => Wallpapers.apply(w.id, {}) })
        }
        for (const k in groups) groups[k].sort((x, y) => y.s - x.s)
        groups.apps = groups.apps.slice(0, 6)
        groups.windows = groups.windows.slice(0, 5)
        groups.marks = groups.marks.slice(0, 3)
        groups.system = groups.system.slice(0, 4)
        groups.walls = groups.walls.slice(0, wallWord ? 8 : 3)

        const push = (group, item) => {
            refs.push(item.run)
            results.append(Object.assign({ group: group, ref: refs.length - 1 }, item.row))
        }

        const value = calc(query)
        if (value !== null)
            push(I18n.tr("Calculadora"), { row: { kind: "calc", title: value, detail: query.trim() + " =",
                iconSource: "", icon: "calculator", hint: I18n.tr("Copiar") },
                run: () => Quickshell.execDetached(["wl-copy", value]) })

        // Top hit: the best score across groups leads, on its own.
        let best = null, bestKey = ""
        for (const k in groups)
            if (groups[k].length && (!best || groups[k][0].s > best.s)) { best = groups[k][0]; bestKey = k }
        if (best && value === null) {
            push(I18n.tr("Mejor resultado"), best)
            groups[bestKey] = groups[bestKey].slice(1)
        }
        for (const [k, label] of [["apps", I18n.tr("Aplicaciones")], ["windows", I18n.tr("Ventanas en el lienzo")],
                                  ["marks", I18n.tr("Marcadores")], ["system", I18n.tr("Sistema")], ["walls", I18n.tr("Fondos de pantalla")]])
            for (const item of groups[k]) push(label, item)

        const raw = query.trim()
        push("Web", { row: { kind: "web", title: I18n.tr("Buscar «%1» en la web").arg(raw), detail: "DuckDuckGo",
            iconSource: "", icon: "globe", hint: I18n.tr("Buscar") },
            run: () => Quickshell.execDetached(["xdg-open", "https://duckduckgo.com/?q=" + encodeURIComponent(raw)]) })

        root.refs = refs
        const sections = new Set()
        for (let i = 0; i < results.count; i++) sections.add(results.get(i).group)
        root.listHeight = Math.min(results.count * 40 + sections.size * 26, 440)
        list.currentIndex = 0
        list.positionViewAtBeginning()
    }

    function activate(i) {
        if (i < 0 || i >= results.count) return
        const run = refs[results.get(i).ref]
        close()
        if (run) run()
    }

    function move(d) {
        if (!results.count) return
        list.currentIndex = (list.currentIndex + d + results.count) % results.count
    }

    ListModel { id: results }

    // ── surfaces ────────────────────────────────────────────────────────
    // A click anywhere outside the card closes it, as on macOS.
    PanelWindow {
        screen: root.screen
        visible: root.open
        WlrLayershell.namespace: "wonderland-spotlight-backdrop"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        exclusionMode: ExclusionMode.Ignore
        anchors { top: true; bottom: true; left: true; right: true }
        color: "transparent"
        MouseArea { anchors.fill: parent; onPressed: root.close() }
    }

    PanelWindow {
        id: win
        screen: root.screen
        visible: root.open
        WlrLayershell.namespace: "wonderland-spotlight"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore
        anchors.top: true
        margins.top: Math.round((root.screen ? root.screen.height : 1080) * 0.2)
        color: "transparent"
        implicitWidth: 680
        implicitHeight: card.height

        onVisibleChanged: if (visible) { field.forceActiveFocus(); intro.restart() }

        Rectangle {
            id: card
            width: parent.width
            height: searchRow.height + (results.count ? divider.height + list.height + 10 : 0)
            radius: 14
            readonly property color base: Theme.p.elevated
            color: Theme.glassy(base, Theme.dark ? 0.86 : 0.90)
            border.width: 1
            border.color: Theme.p.stroke2
            transformOrigin: Item.Top

            ParallelAnimation {
                id: intro
                NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: Theme.animMs }
                NumberAnimation { target: card; property: "scale"; from: 0.97; to: 1; duration: Theme.animMs; easing.type: Easing.OutCubic }
            }

            Item {
                id: searchRow
                width: parent.width
                height: 58

                Icon {
                    id: lens
                    name: "search"
                    size: 22
                    color: Theme.p.text3
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: field
                    anchors.left: lens.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: 22
                    font.weight: Font.Light
                    color: Theme.p.text
                    selectionColor: Theme.p.accentSoft
                    selectedTextColor: Theme.p.text
                    clip: true
                    focus: true
                    text: root.query
                    onTextEdited: { root.query = text; root.rebuild() }

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: !field.text
                        text: I18n.tr("Buscar en WonderlandWM")
                        font: field.font
                        color: Theme.p.text4
                    }

                    Keys.onPressed: event => {
                        const ctrl = event.modifiers & Qt.ControlModifier
                        if (event.key === Qt.Key_Escape) { root.close(); event.accepted = true }
                        else if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab || (ctrl && event.key === Qt.Key_N)) { root.move(1); event.accepted = true }
                        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Backtab || (ctrl && event.key === Qt.Key_P)) { root.move(-1); event.accepted = true }
                        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { root.activate(list.currentIndex); event.accepted = true }
                    }
                }
            }

            Rectangle {
                id: divider
                anchors.top: searchRow.bottom
                width: parent.width
                height: results.count ? 1 : 0
                color: Theme.p.stroke3
            }

            ListView {
                id: list
                anchors.top: divider.bottom
                anchors.topMargin: 5
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                height: root.listHeight
                clip: true
                model: results
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                currentIndex: -1
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                section.property: "group"
                section.delegate: Item {
                    required property string section
                    width: ListView.view.width
                    height: 26
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: parent.section
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        font.weight: Font.DemiBold
                        color: Theme.p.text3
                    }
                }

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string kind
                    required property string title
                    required property string detail
                    required property string icon
                    required property string iconSource
                    required property string hint
                    readonly property bool selected: ListView.isCurrentItem

                    width: ListView.view.width
                    height: 40
                    radius: 8
                    color: selected ? Theme.p.accent : "transparent"

                    Item {
                        id: glyph
                        width: 26
                        height: 26
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter

                        Image {
                            id: appIcon
                            anchors.fill: parent
                            source: row.iconSource
                            sourceSize: Qt.size(row.kind === "wallpaper" ? 96 : 52, 52)
                            fillMode: Image.PreserveAspectCrop
                            visible: row.iconSource !== "" && status === Image.Ready
                            smooth: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: !appIcon.visible
                            radius: 7
                            color: row.selected ? Qt.rgba(1, 1, 1, 0.18) : Theme.p.accentSoft
                            Icon {
                                anchors.centerIn: parent
                                name: row.icon || "app-window"
                                size: 15
                                color: row.selected ? Theme.p.onAccent : Theme.p.accent
                            }
                        }
                    }

                    Row {
                        anchors.left: glyph.right
                        anchors.leftMargin: 12
                        anchors.right: hintLabel.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        clip: true

                        Text {
                            text: row.title
                            font.family: Theme.fontFamily
                            font.pixelSize: row.kind === "calc" ? 20 : 14
                            color: row.selected ? Theme.p.onAccent : Theme.p.text
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, parent.width * 0.7)
                        }
                        Text {
                            text: row.detail ? (row.kind === "calc" ? row.detail : "— " + row.detail) : ""
                            anchors.verticalCenter: parent.verticalCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: row.selected ? Qt.rgba(1, 1, 1, 0.78) : Theme.p.text3
                            elide: Text.ElideRight
                            width: parent.width - x
                        }
                    }

                    Row {
                        id: hintLabel
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        visible: row.selected
                        Text {
                            text: row.hint
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: Qt.rgba(1, 1, 1, 0.85)
                        }
                        Icon { name: "corner-down-left"; size: 13; color: Qt.rgba(1, 1, 1, 0.85); anchors.verticalCenter: parent.verticalCenter }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        // Only a real move selects: rows appearing under a
                        // resting pointer must not steal the top hit.
                        onPositionChanged: list.currentIndex = row.index
                        onClicked: root.activate(row.index)
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "spotlight"
        function toggle(): void { root.toggle() }
        function open(): void { root.show() }
        function close(): void { root.close() }
    }
}
